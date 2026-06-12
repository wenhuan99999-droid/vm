#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// ============ 配置常量 ============
#define MAX_IDENT_LEN 32
#define MAX_TOKEN_LEN 64
#define MAX_CODE_LINES 1000
#define MAX_SYMBOLS 200
#define MAX_SCOPE 50
#define GLOBAL_BASE 100  // 全局变量的基址，局部变量从 0 开始
#define STACK_SIZE 1000
#define MAX_AST_NODES 1000
#define MAX_ARRAY_SIZE 100

// ============ Token 定义 ============
typedef enum {
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_NUM,
    TOKEN_OPERATOR,
    TOKEN_DELIMITER,
    TOKEN_ERROR,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char lexeme[MAX_TOKEN_LEN];
    int line_no;
    int int_value;
} Token;

// ============ AST 定义 ============
typedef struct ASTNode {
    Token token;
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *mid;
    struct ASTNode *next;
    struct ASTNode *case_list;
    int is_array;  // 标记是否是数组
} ASTNode;

// ============ 符号表定义 ============
typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_ARRAY
} SymbolKind;

typedef struct {
    char name[MAX_IDENT_LEN];
    SymbolKind kind;
    char type[MAX_IDENT_LEN];
    int scope;
    int offset;
    int array_size;
    int init_value;  // 初始化值（如果是变量）
    int has_init;    // 是否有初始化值
} Symbol;

// ============ 中间代码定义 ============
typedef struct {
    char op[MAX_IDENT_LEN + 8];  // 足够容纳函数标签 _func_xxx
    char arg[MAX_IDENT_LEN];
    int line_no;
} CodeLine;

// ============ 函数参数定义 ============
typedef struct {
    char name[MAX_IDENT_LEN];
    char type[MAX_IDENT_LEN];
    int offset;
} FuncParam;

typedef struct {
    char name[MAX_IDENT_LEN];
    char return_type[MAX_IDENT_LEN];
    FuncParam params[10];
    int param_count;
    int code_start;
    int local_var_count;
} FuncInfo;

// ============ 全局变量 ============
// 词法分析器
static FILE *source_file = NULL;
static int line = 1;
static int curr_char = ' ';
static bool have_char = false;
static Token current_token;
static Token last_token;
static bool have_last_token = false;

// AST
static ASTNode *ast_nodes[MAX_AST_NODES];
static int ast_node_count = 0;

// 符号表
static Symbol symbol_table[MAX_SYMBOLS];
static int symbol_count = 0;
static int current_scope = 0;
static int current_offset = 0;
static int scope_offsets[MAX_SCOPE];  // 保存每个作用域的起始 offset
static int next_func_offset = 0;  // 下一个函数的起始 offset

// 词法错误缓冲区
#define MAX_LEX_ERRORS 100
static char lex_errors[MAX_LEX_ERRORS][200];
static int lex_error_count = 0;

// 添加词法错误到缓冲区
void add_lex_error(const char *msg, int line) {
    if (lex_error_count < MAX_LEX_ERRORS) {
        // 使用词法错误计数来编号
        sprintf(lex_errors[lex_error_count], "Error %d: Lexical error - %s at line %d", 
                lex_error_count + 1, msg, line);
        lex_error_count++;
    }
}

// 打印所有词法错误
void print_lex_errors() {
    for (int i = 0; i < lex_error_count; i++) {
        printf("%s\n", lex_errors[i]);
    }
}

// Token 缓冲区，用于保存词法分析结果
#define MAX_TOKENS 10000
static Token token_buffer[MAX_TOKENS];
static int token_count = 0;
static int token_pos = 0;

// 中间代码
static CodeLine code[MAX_CODE_LINES];
static int code_count = 0;
static int label_count = 0;

// 函数表
static FuncInfo func_table[MAX_SYMBOLS];
static int func_count = 0;

// 错误处理
static int error_count = 0;
static bool has_errors = false;
#define MAX_ERRORS 20

// 虚拟机状态
static int operand_stack[STACK_SIZE];
static int stack_ptr = 0;
static int data_memory[500];
static int program_counter = 0;
static int return_address_stack[50];
static int return_ptr = 0;

// 关键字列表
static const char *keywords[] = {
    "as", "break", "case", "const", "continue", "default", "do", 
    "else", "for", "from", "func", "if", "in", "int", "let", 
    "main", "match", "read", "return", "switch", "var", "void", 
    "where", "while", "write", "array"
};
#define KEYWORD_COUNT (sizeof(keywords)/sizeof(keywords[0]))

// ============ 函数声明 ============
// 词法分析器
void get_char(void);
bool skip_comments(void);
void next_token(void);
int is_keyword(const char *s);
void read_ident(char *buf, int *len);
bool read_number(char *buf, int *len, int *val);

// 语法分析器
ASTNode* create_node(Token token);
void error(const char *message);
void skip_to_sync_point();
void match_keyword_safe(const char *kw);
void match_delimiter_safe(char d);
void match_keyword(const char *kw);
void match_delimiter(char d);

ASTNode* program();
ASTNode* fun_declaration();
ASTNode* main_declaration();
ASTNode* fun_body();
ASTNode* declaration_list();
ASTNode* declaration_stat();
ASTNode* statement_list();
ASTNode* statement();
ASTNode* if_stat();
ASTNode* while_stat();
ASTNode* do_while_stat();
ASTNode* for_stat();
ASTNode* switch_stat();
ASTNode* case_list();
ASTNode* read_stat();
ASTNode* write_stat();
ASTNode* compound_stat();
ASTNode* expression_stat();
ASTNode* return_stat();
ASTNode* break_stat();
ASTNode* continue_stat();
ASTNode* expression();
ASTNode* bool_expr();
ASTNode* logical_and_expr();
ASTNode* logical_or_expr();
ASTNode* additive_expr();
ASTNode* term();
ASTNode* factor();

// 语义分析和代码生成
int lookup_symbol(const char *name, int scope);
int insert_symbol(const char *name, SymbolKind kind, const char *type, int scope, int array_size);
void enter_scope(void);
void exit_scope(void);
void generate_code(const char *op, const char *arg, int line);
char* new_label(const char *prefix);
int find_label(const char *label);
void process_node(ASTNode *node);
int find_function(const char *name);
void push_return_address(int addr);
int pop_return_address(void);

// 虚拟机执行
void vm_execute(void);
void vm_push(int value);
int vm_pop(void);
void print_code(FILE *out);
void print_symbol_table(FILE *out);

// ============ 词法分析器实现 ============
void get_char(void) {
    int c = fgetc(source_file);
    if (c == EOF) {
        have_char = false;
        curr_char = '\0';
    } else {
        have_char = true;
        curr_char = c;
        if (curr_char == '\n') line++;
    }
}

bool skip_comments(void) {
    while (have_char) {
        if (curr_char == ' ' || curr_char == '\t' || curr_char == '\n' || curr_char == '\r') {
            get_char();
            continue;
        }
        if (curr_char == '/') {
            get_char();
            if (!have_char) break;
            if (curr_char == '/') {
                while (have_char && curr_char != '\n') get_char();
                if (have_char) get_char();
                continue;
            } else if (curr_char == '*') {
                get_char();
                bool closed = false;
                while (have_char) {
                    if (curr_char == '*') {
                        get_char();
                        if (curr_char == '/') {
                            closed = true;
                            get_char();
                            break;
                        }
                    } else {
                        get_char();
                    }
                }
                if (!closed) {
                    fprintf(stderr, "Lexical Error: unclosed comment at line %d\n", line);
                    return false;
                }
                continue;
            } else {
                ungetc('/', source_file);
                get_char();
                break;
            }
        }
        break;
    }
    return true;
}

int is_keyword(const char *s) {
    for (int i = 0; i < KEYWORD_COUNT; i++) {
        if (strcmp(s, keywords[i]) == 0) return i + 1;
    }
    return 0;
}

void read_ident(char *buf, int *len) {
    *len = 0;
    while (have_char && (isalnum(curr_char) || curr_char == '_')) {
        if (*len < MAX_IDENT_LEN - 1) buf[(*len)++] = curr_char;
        get_char();
    }
    buf[*len] = '\0';
}

bool read_number(char *buf, int *len, int *val) {
    *len = 0;
    *val = 0;
    while (have_char && isdigit(curr_char)) {
        if (*len < MAX_TOKEN_LEN - 1) buf[(*len)++] = curr_char;
        *val = *val * 10 + (curr_char - '0');
        get_char();
    }
    buf[*len] = '\0';
    return true;
}

void unget_token(void) {
    // 保存当前 token，以便稍后恢复
    last_token = current_token;
    have_last_token = true;
}

void next_token(void) {
    // 如果有未消费的 token，先返回它
    if (have_last_token) {
        current_token = last_token;
        have_last_token = false;
        return;
    }
    
    // 如果 source_file 为 NULL，说明已经完成词法分析，从缓冲区读取
    if (!source_file) {
        if (token_pos < token_count) {
            current_token = token_buffer[token_pos];
            token_pos++;
        } else {
            Token eof_token = {.type = TOKEN_EOF, .lexeme = "EOF", .line_no = 0, .int_value = 0};
            current_token = eof_token;
        }
        return;
    }
    
    Token tok = {.type = TOKEN_EOF, .lexeme = "", .line_no = line, .int_value = 0};

    if (!skip_comments()) {
        tok.type = TOKEN_ERROR;
        strcpy(tok.lexeme, "Unclosed comment");
        // 添加词法错误到缓冲区
        add_lex_error(tok.lexeme, line);
        error_count++;
        has_errors = true;
        current_token = tok;
        return;
    }

    if (!have_char) {
        tok.type = TOKEN_EOF;
        strcpy(tok.lexeme, "EOF");
        current_token = tok;
        return;
    }

    if (isalpha(curr_char) || curr_char == '_') {
        char buf[MAX_IDENT_LEN];
        int len;
        read_ident(buf, &len);
        if (is_keyword(buf)) {
            tok.type = TOKEN_KEYWORD;
            strcpy(tok.lexeme, buf);
        } else {
            tok.type = TOKEN_IDENTIFIER;
            strcpy(tok.lexeme, buf);
        }
        current_token = tok;
        return;
    }

    if (isdigit(curr_char)) {
        char buf[MAX_TOKEN_LEN];
        int len;
        if (!read_number(buf, &len, &tok.int_value)) {
            tok.type = TOKEN_ERROR;
            strcpy(tok.lexeme, buf);
            // 添加词法错误到缓冲区
            char msg[MAX_TOKEN_LEN + 20];
            sprintf(msg, "Invalid number '%s'", buf);
            add_lex_error(msg, line);
            error_count++;
            has_errors = true;
            current_token = tok;
            return;
        }
        tok.type = TOKEN_NUM;
        strcpy(tok.lexeme, buf);
        current_token = tok;
        return;
    }

    char c = curr_char;
    tok.lexeme[0] = c;
    tok.lexeme[1] = '\0';
    get_char();

    if (have_char && (c == '=' || c == '!' || c == '<' || c == '>')) {
        char n = curr_char;
        if (n == '=') {
            tok.lexeme[1] = n;
            tok.lexeme[2] = '\0';
            get_char();
            tok.type = TOKEN_OPERATOR;
            current_token = tok;
            return;
        }
    }
    if (c == '.' && have_char && curr_char == '.') {
        tok.lexeme[1] = curr_char;
        tok.lexeme[2] = '\0';
        get_char();
        tok.type = TOKEN_DELIMITER;
        current_token = tok;
        return;
    }

    switch (c) {
        case '+':
        case '*':
        case '/':
        case '%':
        case '=':
        case '!':
        case '<':
        case '>':
        case '&':
            tok.type = TOKEN_OPERATOR;
            if (have_char && curr_char == '&') {
                tok.lexeme[1] = '&';
                tok.lexeme[2] = '\0';
                get_char();
            }
            break;
        case '|':
            tok.type = TOKEN_OPERATOR;
            if (have_char && curr_char == '|') {
                tok.lexeme[1] = '|';
                tok.lexeme[2] = '\0';
                get_char();
            }
            break;
        case '-':
            if (have_char && curr_char == '>') {
                tok.lexeme[1] = curr_char;
                tok.lexeme[2] = '\0';
                get_char();
            }
            tok.type = TOKEN_OPERATOR;
            break;
        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
        case ';':
        case ':':
        case ',':
            tok.type = TOKEN_DELIMITER;
            break;
        default:
            tok.type = TOKEN_ERROR;
            sprintf(tok.lexeme, "Unknown character '%c'", c);
            // 添加词法错误到缓冲区
            add_lex_error(tok.lexeme, line);
            error_count++;
            has_errors = true;
            // 跳过这个错误字符，继续处理下一个字符
            get_char();
            // 递归调用 next_token 继续处理，不返回错误 token
            next_token();
            return;
    }
    current_token = tok;
}

// ============ 语法分析器实现 ============
ASTNode* create_node(Token token) {
    if (ast_node_count >= MAX_AST_NODES) {
        fprintf(stderr, "AST node limit exceeded\n");
        exit(1);
    }
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    node->token = token;
    node->left = NULL;
    node->right = NULL;
    node->mid = NULL;
    node->next = NULL;
    node->case_list = NULL;
    ast_nodes[ast_node_count++] = node;
    return node;
}

void error(const char *message) {
    if (error_count < MAX_ERRORS) {
        fprintf(stderr, "Error %d: %s at line %d\n", error_count + 1, message, current_token.line_no);
        error_count++;
        has_errors = true;
    } else if (error_count == MAX_ERRORS) {
        fprintf(stderr, "Too many errors. Stopping error reporting.\n");
        error_count++;
    }
}

void skip_to_sync_point() {
    int brace_depth = 0;
    int paren_depth = 0;
    while (current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_DELIMITER) {
            switch (current_token.lexeme[0]) {
                case ';':
                    // 只有在没有未闭合的括号时才在分号处停止
                    if (brace_depth == 0 && paren_depth == 0) {
                        return;
                    }
                    break;
                case ')':
                    paren_depth--;
                    if (paren_depth < 0) {
                        // 找到匹配的右括号，停止
                        return;
                    }
                    break;
                case '(':
                    paren_depth++;
                    break;
                case ']':
                    if (brace_depth == 0) {
                        return;
                    }
                    break;
                case '{':
                    brace_depth++;
                    break;
                case '}':
                    brace_depth--;
                    if (brace_depth == 0) {
                        // 找到函数体的结束，停止
                        return;
                    }
                    break;
            }
        }
        if (current_token.type == TOKEN_KEYWORD && brace_depth == 0 && paren_depth == 0) {
            const char *keywords_sync[] = {"var", "let", "const", "if", "while", "do", "for", "switch", "read", "write", "return", "break", "continue", "main", "func", "case", "default"};
            for (int i = 0; i < 17; i++) {
                if (strcmp(current_token.lexeme, keywords_sync[i]) == 0) {
                    return;
                }
            }
        }
        next_token();
        static int skip_count = 0;
        if (skip_count++ > 1000) {
            fprintf(stderr, "Warning: skipping too many tokens, breaking loop\n");
            return;
        }
    }
}

// 跳过到下一个函数定义（func 或 main 关键字）
// 这用于在函数内部遇到严重语法错误时，跳过整个函数
void skip_to_next_function() {
    int brace_depth = 0;
    while (current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_DELIMITER) {
            if (current_token.lexeme[0] == '{') {
                brace_depth++;
            } else if (current_token.lexeme[0] == '}') {
                brace_depth--;
                // 当花括号深度回到0时，说明函数体已经结束
                if (brace_depth == 0) {
                    next_token();  // 消费 }
                    return;
                }
            }
        }
        // 只有在花括号深度为0时，才检查函数定义关键字
        if (brace_depth == 0 && current_token.type == TOKEN_KEYWORD) {
            if (strcmp(current_token.lexeme, "func") == 0 ||
                strcmp(current_token.lexeme, "main") == 0) {
                return;
            }
        }
        next_token();
        static int skip_count = 0;
        if (skip_count++ > 1000) {
            fprintf(stderr, "Warning: skipping too many tokens in function, breaking\n");
            return;
        }
    }
}

void match_keyword_safe(const char *kw) {
    if (current_token.type == TOKEN_KEYWORD && strcmp(current_token.lexeme, kw) == 0) {
        next_token();
    } else {
        char msg[100];
        sprintf(msg, "expected '%s', got '%s'", kw, current_token.lexeme);
        error(msg);
        skip_to_sync_point();
    }
}

void match_delimiter_safe(char d) {
    if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == d) {
        next_token();
    } else {
        char msg[100];
        sprintf(msg, "expected '%c'", d);
        error(msg);
        skip_to_sync_point();
        // 如果找到的同步点就是我们需要的分隔符，就消费它
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == d) {
            next_token();
        }
    }
}

void match_keyword(const char *kw) {
    if (current_token.type == TOKEN_KEYWORD && strcmp(current_token.lexeme, kw) == 0) {
        next_token();
    } else {
        fprintf(stderr, "Error: expected '%s' at line %d, got '%s'\n", kw, current_token.line_no, current_token.lexeme);
        exit(1);
    }
}

void match_delimiter(char d) {
    if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == d) {
        next_token();
    } else {
        fprintf(stderr, "Error: expected '%c' at line %d\n", d, current_token.line_no);
        exit(1);
    }
}

ASTNode* factor() {
    ASTNode *node = NULL;
    Token token = current_token;

    if (current_token.type == TOKEN_IDENTIFIER) {
        next_token();
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '[') {
            // 数组访问
            match_delimiter_safe('[');
            ASTNode *index_node = expression();
            match_delimiter_safe(']');
            ASTNode *arr_node = create_node(token);
            arr_node->left = index_node;
            return arr_node;
        } else if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '(') {
            // 函数调用 - 在 factor 中处理，因为表达式解析从 factor 开始
            match_delimiter_safe('(');
            ASTNode *args = NULL;
            ASTNode *args_last = NULL;
            if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ')') {
                args = expression();
                args_last = args;
                while (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ',') {
                    match_delimiter_safe(',');
                    ASTNode *arg = expression();
                    args_last->next = arg;
                    args_last = arg;
                }
            }
            match_delimiter_safe(')');
            ASTNode *call_node = create_node(token);
            call_node->mid = args;
            return call_node;
        }
        // 如果不是数组访问或函数调用，返回标识符节点
        return create_node(token);
    } else if (current_token.type == TOKEN_NUM) {
        next_token();
        node = create_node(token);
    } else if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '(') {
        match_delimiter_safe('(');
        node = logical_or_expr();
        match_delimiter_safe(')');
    } else if (current_token.type != TOKEN_EOF) {
        char msg[100];
        sprintf(msg, "unexpected token in factor: '%s'", current_token.lexeme);
        error(msg);
        skip_to_sync_point();
    }
    return node;
}

ASTNode* term() {
    ASTNode *left = factor();
    Token token;

    while (current_token.type == TOKEN_OPERATOR &&
           (strcmp(current_token.lexeme, "*") == 0 || strcmp(current_token.lexeme, "/") == 0 || strcmp(current_token.lexeme, "%") == 0)) {
        token = current_token;
        next_token();
        ASTNode *right = factor();
        ASTNode *new_node = create_node(token);
        new_node->left = left;
        new_node->right = right;
        left = new_node;
    }
    return left;
}

ASTNode* additive_expr() {
    ASTNode *left = term();
    Token token;

    while (current_token.type == TOKEN_OPERATOR &&
           (strcmp(current_token.lexeme, "+") == 0 || strcmp(current_token.lexeme, "-") == 0)) {
        token = current_token;
        next_token();
        ASTNode *right = term();
        ASTNode *new_node = create_node(token);
        new_node->left = left;
        new_node->right = right;
        left = new_node;
    }
    return left;
}

ASTNode* bool_expr() {
    ASTNode *left = additive_expr();
    const char *rel_str[] = {">", "<", ">=", "<=", "==", "!="};
    int i;
    for (i = 0; i < 6; i++) {
        if (current_token.type == TOKEN_OPERATOR && strcmp(current_token.lexeme, rel_str[i]) == 0) {
            Token token = current_token;
            next_token();
            ASTNode *right = additive_expr();
            ASTNode *new_node = create_node(token);
            new_node->left = left;
            new_node->right = right;
            return new_node;
        }
    }
    return left;
}

ASTNode* logical_and_expr() {
    ASTNode *left = bool_expr();
    
    if (current_token.type == TOKEN_OPERATOR && strcmp(current_token.lexeme, "&&") == 0) {
        Token token = current_token;
        next_token();
        ASTNode *right = logical_and_expr();
        ASTNode *new_node = create_node(token);
        new_node->left = left;
        new_node->right = right;
        return new_node;
    }
    return left;
}

ASTNode* logical_or_expr() {
    ASTNode *left = logical_and_expr();
    
    if (current_token.type == TOKEN_OPERATOR && strcmp(current_token.lexeme, "||") == 0) {
        Token token = current_token;
        next_token();
        ASTNode *right = logical_or_expr();
        ASTNode *new_node = create_node(token);
        new_node->left = left;
        new_node->right = right;
        return new_node;
    }
    return left;
}

ASTNode* expression() {
    // expression 处理表达式，包括赋值语句
    // 先调用 logical_or_expr() 解析表达式
    ASTNode *left = logical_or_expr();
    
    // 如果是赋值语句
    if (left && current_token.type == TOKEN_OPERATOR && strcmp(current_token.lexeme, "=") == 0) {
        Token assign_token = current_token;
        next_token();
        ASTNode *expr_node = logical_or_expr();
        ASTNode *node = create_node(assign_token);
        node->left = left;
        node->right = expr_node;
        return node;
    }
    
    return left;
}

ASTNode* declaration_stat() {
    Token kind_token = current_token;
    if (current_token.type == TOKEN_KEYWORD &&
        (strcmp(current_token.lexeme, "var") == 0 || strcmp(current_token.lexeme, "let") == 0 || strcmp(current_token.lexeme, "const") == 0)) {
        next_token();
    } else if (current_token.type != TOKEN_EOF) {
        error("expected var/let/const");
        skip_to_sync_point();
        return NULL;
    } else {
        return NULL;
    }

    ASTNode *decl_list = NULL;
    ASTNode *decl_last = NULL;

    while (1) {
        Token id_token = current_token;
        if (current_token.type != TOKEN_IDENTIFIER) {
            error("expected identifier");
            while (current_token.type != TOKEN_EOF &&
                   !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';')) {
                next_token();
            }
            if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
                next_token();
            }
            return decl_list;
        }
        next_token();

        int is_array = 0;
        int array_size = 0;
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '[') {
            match_delimiter_safe('[');
            if (current_token.type == TOKEN_NUM) {
                array_size = current_token.int_value;
                next_token();
            }
            match_delimiter_safe(']');
            is_array = 1;
        }

        if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ':') {
            error("expected ':'");
            while (current_token.type != TOKEN_EOF &&
                   !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') &&
                   !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ',')) {
                next_token();
            }
            if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
                next_token();
            }
            ASTNode *node = create_node(kind_token);
            ASTNode *id_node = create_node(id_token);
            node->left = id_node;
            if (!decl_list) decl_list = node;
            else decl_last->next = node;
            decl_last = node;
            if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ',') {
                match_delimiter_safe(',');
                continue;
            }
            return decl_list;
        }
        next_token();

        Token type_token;
        if (current_token.type == TOKEN_KEYWORD && (strcmp(current_token.lexeme, "int") == 0 || strcmp(current_token.lexeme, "void") == 0)) {
            type_token = current_token;
            next_token();
        } else {
            error("expected 'int'");
            while (current_token.type != TOKEN_EOF &&
                   !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') &&
                   !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ',')) {
                next_token();
            }
            if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
                next_token();
            }
            ASTNode *node = create_node(kind_token);
            ASTNode *id_node = create_node(id_token);
            node->left = id_node;
            type_token.type = TOKEN_KEYWORD;
            strcpy(type_token.lexeme, "int");
            ASTNode *type_node = create_node(type_token);
            node->right = type_node;
            if (!decl_list) decl_list = node;
            else decl_last->next = node;
            decl_last = node;
            if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ',') {
                match_delimiter_safe(',');
                continue;
            }
            return decl_list;
        }

        ASTNode *node = create_node(kind_token);
        ASTNode *id_node = create_node(id_token);
        ASTNode *type_node = create_node(type_token);

        node->left = id_node;
        node->right = type_node;
        node->is_array = 0;

        if (is_array) {
            Token size_token;
            size_token.type = TOKEN_NUM;
            sprintf(size_token.lexeme, "%d", array_size);
            size_token.int_value = array_size;
            ASTNode *size_node = create_node(size_token);
            node->mid = size_node;
            node->is_array = 1;
        }

        if (current_token.type == TOKEN_OPERATOR && strcmp(current_token.lexeme, "=") == 0) {
            next_token();
            ASTNode *expr_node = expression();
            // 如果是数组，不能有初始化值，只保留数组大小
            if (!is_array) {
                node->mid = expr_node;
            }
        }

        if (!decl_list) decl_list = node;
        else decl_last->next = node;
        decl_last = node;

        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ',') {
            match_delimiter_safe(',');
            continue;
        }

        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
            match_delimiter_safe(';');
        }

        break;
    }

    return decl_list;
}

ASTNode* declaration_list() {
    ASTNode *root = NULL;
    ASTNode *prev = NULL;

    while (current_token.type == TOKEN_KEYWORD &&
           (strcmp(current_token.lexeme, "var") == 0 || strcmp(current_token.lexeme, "let") == 0 || strcmp(current_token.lexeme, "const") == 0)) {
        ASTNode *decl_node = declaration_stat();
        if (!decl_node) continue;

        ASTNode *temp = decl_node;
        while (temp) {
            if (root == NULL) {
                root = temp;
            } else {
                prev->next = temp;
            }
            prev = temp;
            ASTNode *next_temp = temp->next;
            temp->next = NULL;
            temp = next_temp;
        }
    }

    return root;
}

ASTNode* if_stat() {
    match_keyword_safe("if");
    match_delimiter_safe('(');

    ASTNode *cond = logical_or_expr();

    match_delimiter_safe(')');

    ASTNode *then_stmt = statement();

    ASTNode *else_stmt = NULL;
    if (current_token.type == TOKEN_KEYWORD && strcmp(current_token.lexeme, "else") == 0) {
        match_keyword_safe("else");
        else_stmt = statement();
    }

    Token if_token;
    if_token.type = TOKEN_KEYWORD;
    strcpy(if_token.lexeme, "if");

    ASTNode *node = create_node(if_token);
    node->left = cond;
    node->mid = then_stmt;
    node->right = else_stmt;

    return node;
}

ASTNode* while_stat() {
    match_keyword_safe("while");

    if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != '(') {
        error("expected '('");
        while (current_token.type != TOKEN_EOF &&
               !(current_token.type == TOKEN_KEYWORD) &&
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}') &&
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '{')) {
            next_token();
        }
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '{') {
            ASTNode *body = compound_stat();
            Token while_token;
            while_token.type = TOKEN_KEYWORD;
            strcpy(while_token.lexeme, "while");
            ASTNode *node = create_node(while_token);
            node->right = body;
            return node;
        }
        Token while_token;
        while_token.type = TOKEN_KEYWORD;
        strcpy(while_token.lexeme, "while");
        return create_node(while_token);
    }
    next_token();

    ASTNode *cond = logical_or_expr();

    if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ')') {
        error("expected ')'");
        // 跳过整个函数，避免影响后续解析
        skip_to_next_function();
        // 返回一个空节点
        Token while_token;
        while_token.type = TOKEN_KEYWORD;
        strcpy(while_token.lexeme, "while");
        return create_node(while_token);
    }

    next_token();
    ASTNode *body = statement();
    Token while_token;
    while_token.type = TOKEN_KEYWORD;
    strcpy(while_token.lexeme, "while");

    ASTNode *node = create_node(while_token);
    node->left = cond;
    node->right = body;

    return node;
}

ASTNode* do_while_stat() {
    match_keyword_safe("do");

    ASTNode *body = statement();

    match_keyword_safe("while");
    match_delimiter_safe('(');

    ASTNode *cond = logical_or_expr();

    match_delimiter_safe(')');
    match_delimiter_safe(';');

    Token do_token;
    do_token.type = TOKEN_KEYWORD;
    strcpy(do_token.lexeme, "do");

    ASTNode *node = create_node(do_token);
    node->left = body;
    node->right = cond;

    return node;
}

ASTNode* for_stat() {
    match_keyword_safe("for");
    match_delimiter_safe('(');

    Token id_token = current_token;
    if (current_token.type != TOKEN_IDENTIFIER) {
        error("expected identifier");
        skip_to_sync_point();
        return NULL;
    }
    next_token();

    match_keyword_safe("in");

    ASTNode *expr_node = logical_or_expr();

    match_delimiter_safe(')');

    ASTNode *body = statement();

    Token for_token;
    for_token.type = TOKEN_KEYWORD;
    strcpy(for_token.lexeme, "for");

    ASTNode *node = create_node(for_token);
    ASTNode *id_node = create_node(id_token);
    node->left = id_node;
    node->mid = expr_node;
    node->right = body;

    return node;
}

ASTNode* case_list() {
    ASTNode *root = NULL;
    ASTNode *prev = NULL;

    while (current_token.type == TOKEN_KEYWORD && 
           (strcmp(current_token.lexeme, "case") == 0 || strcmp(current_token.lexeme, "default") == 0)) {
        Token case_token = current_token;
        next_token();

        ASTNode *case_val = NULL;
        if (strcmp(case_token.lexeme, "case") == 0) {
            if (current_token.type == TOKEN_NUM) {
                case_val = create_node(current_token);
                next_token();
            } else {
                error("expected constant after case");
            }
        }

        match_delimiter_safe(':');

        ASTNode *stmt_list = statement_list();

        ASTNode *case_node = create_node(case_token);
        case_node->left = case_val;
        case_node->right = stmt_list;

        if (root == NULL) {
            root = case_node;
        } else {
            prev->next = case_node;
        }
        prev = case_node;
    }

    return root;
}

ASTNode* switch_stat() {
    match_keyword_safe("switch");
    match_delimiter_safe('(');
    
    ASTNode *expr = expression();
    
    match_delimiter_safe(')');
    match_delimiter_safe('{');
    
    ASTNode *cases = case_list();

    match_delimiter_safe('}');

    Token switch_token;
    switch_token.type = TOKEN_KEYWORD;
    strcpy(switch_token.lexeme, "switch");

    ASTNode *node = create_node(switch_token);
    node->left = expr;
    node->right = cases;

    return node;
}

ASTNode* read_stat() {
    match_keyword_safe("read");

    Token id_token = current_token;
    if (current_token.type != TOKEN_IDENTIFIER) {
        error("expected identifier");
        skip_to_sync_point();
        return NULL;
    }
    next_token();

    if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '[') {
        match_delimiter_safe('[');
        ASTNode *index_node = expression();
        match_delimiter_safe(']');
        match_delimiter_safe(';');

        Token read_token;
        read_token.type = TOKEN_KEYWORD;
        strcpy(read_token.lexeme, "read");

        ASTNode *node = create_node(read_token);
        ASTNode *arr_node = create_node(id_token);
        arr_node->left = index_node;
        node->left = arr_node;

        return node;
    }

    match_delimiter_safe(';');

    Token read_token;
    read_token.type = TOKEN_KEYWORD;
    strcpy(read_token.lexeme, "read");

    ASTNode *node = create_node(read_token);
    ASTNode *id_node = create_node(id_token);
    node->left = id_node;

    return node;
}

ASTNode* write_stat() {
    match_keyword_safe("write");

    ASTNode *expr_node = expression();

    if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
        next_token();
    } else if (current_token.type == TOKEN_KEYWORD &&
               (strcmp(current_token.lexeme, "case") == 0 ||
                strcmp(current_token.lexeme, "default") == 0)) {
        // 在switch中，write语句后面可以直接跟case或default，不需要分号
    } else if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}') {
        // 在case块的末尾，write语句后面可以直接跟结束花括号
    } else if (current_token.type == TOKEN_EOF) {
        // 文件结束，不需要分号
    } else {
        error("expected ';'");
        while (current_token.type != TOKEN_EOF &&
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') &&
               !(current_token.type == TOKEN_KEYWORD) &&
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}')) {
            if (current_token.type == TOKEN_KEYWORD &&
                (strcmp(current_token.lexeme, "case") == 0 ||
                 strcmp(current_token.lexeme, "default") == 0)) {
                break;
            }
            next_token();
        }
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
            next_token();
        }
    }

    Token write_token;
    write_token.type = TOKEN_KEYWORD;
    strcpy(write_token.lexeme, "write");

    ASTNode *node = create_node(write_token);
    node->left = expr_node;

    return node;
}

ASTNode* return_stat() {
    match_keyword_safe("return");

    ASTNode *expr_node = NULL;
    if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ';') {
        expr_node = expression();
    }

    match_delimiter_safe(';');

    Token return_token;
    return_token.type = TOKEN_KEYWORD;
    strcpy(return_token.lexeme, "return");

    ASTNode *node = create_node(return_token);
    node->left = expr_node;

    return node;
}

ASTNode* break_stat() {
    match_keyword_safe("break");
    match_delimiter_safe(';');

    Token break_token;
    break_token.type = TOKEN_KEYWORD;
    strcpy(break_token.lexeme, "break");

    return create_node(break_token);
}

ASTNode* continue_stat() {
    match_keyword_safe("continue");
    match_delimiter_safe(';');

    Token continue_token;
    continue_token.type = TOKEN_KEYWORD;
    strcpy(continue_token.lexeme, "continue");

    return create_node(continue_token);
}

ASTNode* compound_stat() {
    match_delimiter_safe('{');

    ASTNode *decl_list = declaration_list();
    ASTNode *stmt_list = statement_list();

    match_delimiter_safe('}');

    Token brace_token;
    brace_token.type = TOKEN_DELIMITER;
    strcpy(brace_token.lexeme, "{");

    ASTNode *node = create_node(brace_token);
    node->left = decl_list;
    node->right = stmt_list;

    return node;
}

ASTNode* expression_stat() {
    ASTNode *expr_node = expression();
    if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
        match_delimiter_safe(';');
    }
    return expr_node;
}

ASTNode* statement() {
    ASTNode *node = NULL;

    if (current_token.type == TOKEN_KEYWORD) {
        if (strcmp(current_token.lexeme, "if") == 0) {
            node = if_stat();
        } else if (strcmp(current_token.lexeme, "while") == 0) {
            node = while_stat();
        } else if (strcmp(current_token.lexeme, "do") == 0) {
            node = do_while_stat();
        } else if (strcmp(current_token.lexeme, "for") == 0) {
            node = for_stat();
        } else if (strcmp(current_token.lexeme, "switch") == 0) {
            node = switch_stat();
        } else if (strcmp(current_token.lexeme, "read") == 0) {
            node = read_stat();
        } else if (strcmp(current_token.lexeme, "write") == 0) {
            node = write_stat();
        } else if (strcmp(current_token.lexeme, "return") == 0) {
            node = return_stat();
        } else if (strcmp(current_token.lexeme, "break") == 0) {
            node = break_stat();
        } else if (strcmp(current_token.lexeme, "continue") == 0) {
            node = continue_stat();
        } else if (strcmp(current_token.lexeme, "var") == 0 ||
                   strcmp(current_token.lexeme, "let") == 0 ||
                   strcmp(current_token.lexeme, "const") == 0) {
            char msg[100];
            sprintf(msg, "unexpected declaration keyword '%s' in statement context", current_token.lexeme);
            error(msg);
            skip_to_sync_point();
        } else {
            char msg[100];
            sprintf(msg, "unexpected keyword '%s'", current_token.lexeme);
            error(msg);
            skip_to_sync_point();
        }
    } else if (current_token.type == TOKEN_DELIMITER) {
        if (current_token.lexeme[0] == '{') {
            node = compound_stat();
        } else if (current_token.lexeme[0] == '}') {
            error("unexpected '}'");
            next_token();
        } else if (current_token.lexeme[0] == ';') {
            error("unexpected ';'");
            next_token();
        } else {
            char msg[100];
            sprintf(msg, "unexpected delimiter '%s'", current_token.lexeme);
            error(msg);
            next_token();
        }
    } else if (current_token.type == TOKEN_IDENTIFIER) {
        // 标识符可能是变量、函数调用等，交给 expression_stat 处理
        node = expression_stat();
    } else if (current_token.type == TOKEN_NUM ||
               (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '(')) {
        node = expression_stat();
    } else if (current_token.type == TOKEN_OPERATOR) {
        char msg[100];
        sprintf(msg, "unexpected operator '%s' at statement start", current_token.lexeme);
        error(msg);
        skip_to_sync_point();
    } else if (current_token.type != TOKEN_EOF) {
        char msg[100];
        sprintf(msg, "unexpected token '%s'", current_token.lexeme);
        error(msg);
        next_token();
    }

    return node;
}

ASTNode* statement_list() {
    ASTNode *root = NULL;
    ASTNode *prev = NULL;

    while (current_token.type != TOKEN_EOF) {
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}') {
            // 不消费 '}'，让调用者处理
            break;
        }

        // 在switch语句中，遇到case或default时停止，不消费token
        if (current_token.type == TOKEN_KEYWORD &&
            (strcmp(current_token.lexeme, "case") == 0 ||
             strcmp(current_token.lexeme, "default") == 0)) {
            break;
        }

        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}') {
            error("unexpected '}'");
            next_token();
            continue;
        }

        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
            error("unexpected ';'");
            next_token();
            continue;
        }

        if (current_token.type == TOKEN_OPERATOR) {
            char msg[100];
            sprintf(msg, "unexpected operator '%s' at statement start", current_token.lexeme);
            error(msg);
            while (current_token.type != TOKEN_EOF &&
                   !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') &&
                   !(current_token.type == TOKEN_KEYWORD)) {
                next_token();
            }
            if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') {
                next_token();
            }
            continue;
        }

        // 允许声明出现在语句列表的任何位置
        if (current_token.type == TOKEN_KEYWORD &&
            (strcmp(current_token.lexeme, "var") == 0 ||
             strcmp(current_token.lexeme, "let") == 0 ||
             strcmp(current_token.lexeme, "const") == 0)) {
            // 解析声明语句
            ASTNode *decl_node = declaration_stat();
            if (decl_node) {
                // 将声明节点添加到语句列表中
                if (root == NULL) {
                    root = decl_node;
                } else {
                    prev->next = decl_node;
                }
                // 处理声明链表中的所有节点
                ASTNode *last_decl = decl_node;
                while (last_decl->next) {
                    last_decl = last_decl->next;
                }
                prev = last_decl;
            }
            continue;
        }

        ASTNode *stmt_node = statement();

        if (stmt_node == NULL) {
            continue;
        }

        if (root == NULL) {
            root = stmt_node;
        } else {
            prev->next = stmt_node;
        }
        prev = stmt_node;
    }

    return root;
}

ASTNode* fun_body() {
    match_delimiter('{');

    ASTNode *decl_list = declaration_list();
    ASTNode *stmt_list = statement_list();

    match_delimiter('}');

    Token body_token;
    body_token.type = TOKEN_DELIMITER;
    strcpy(body_token.lexeme, "{");

    ASTNode *node = create_node(body_token);
    node->left = decl_list;
    node->right = stmt_list;

    return node;
}

ASTNode* fun_declaration() {
    match_keyword("func");

    Token id_token = current_token;
    if (current_token.type != TOKEN_IDENTIFIER) {
        fprintf(stderr, "Error: expected function name at line %d\n", current_token.line_no);
        exit(1);
    }
    next_token();

    match_delimiter('(');

    ASTNode *params = NULL;
    ASTNode *params_last = NULL;

    if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ')') {
        while (1) {
            Token param_id = current_token;
            if (current_token.type != TOKEN_IDENTIFIER) {
                fprintf(stderr, "Error: expected parameter name at line %d\n", current_token.line_no);
                exit(1);
            }
            next_token();

            match_delimiter(':');

            Token param_type = current_token;
            if (current_token.type != TOKEN_KEYWORD || (strcmp(current_token.lexeme, "int") != 0 && strcmp(current_token.lexeme, "void") != 0)) {
                fprintf(stderr, "Error: expected type at line %d\n", current_token.line_no);
                exit(1);
            }
            next_token();

            ASTNode *param_node = create_node(param_id);
            ASTNode *type_node = create_node(param_type);
            param_node->right = type_node;

            if (!params) {
                params = param_node;
                params_last = param_node;
            } else {
                params_last->next = param_node;
                params_last = param_node;
            }

            if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ',') {
                break;
            }
            match_delimiter(',');
        }
    }

    match_delimiter(')');

    char return_type[MAX_IDENT_LEN] = "void";
    if ((current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ':') ||
        (current_token.type == TOKEN_OPERATOR && strcmp(current_token.lexeme, "->") == 0)) {
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ':') {
            match_delimiter(':');
        } else {
            next_token(); // consume ->
        }
        if (current_token.type != TOKEN_KEYWORD || (strcmp(current_token.lexeme, "int") != 0 && strcmp(current_token.lexeme, "void") != 0)) {
            fprintf(stderr, "Error: expected return type at line %d\n", current_token.line_no);
            exit(1);
        }
        strcpy(return_type, current_token.lexeme);
        next_token();
    }

    ASTNode *body = fun_body();

    Token func_token;
    func_token.type = TOKEN_KEYWORD;
    strcpy(func_token.lexeme, "func");

    ASTNode *node = create_node(func_token);
    ASTNode *name_node = create_node(id_token);
    node->left = name_node;
    node->mid = params;
    node->right = body;

    Token ret_type_token;
    ret_type_token.type = TOKEN_KEYWORD;
    strcpy(ret_type_token.lexeme, return_type);
    ASTNode *ret_type_node = create_node(ret_type_token);
    name_node->right = ret_type_node;

    return node;
}

ASTNode* main_declaration() {
    match_keyword("main");
    match_delimiter('(');
    match_delimiter(')');

    ASTNode *body = fun_body();

    Token main_token;
    main_token.type = TOKEN_KEYWORD;
    strcpy(main_token.lexeme, "main");

    ASTNode *node = create_node(main_token);
    node->right = body;

    return node;
}

ASTNode* program() {
    ASTNode *root = NULL;
    ASTNode *prev = NULL;

    // 功能5: 支持全局变量声明 (在函数定义之前)
    while (current_token.type == TOKEN_KEYWORD &&
           (strcmp(current_token.lexeme, "var") == 0 || 
            strcmp(current_token.lexeme, "let") == 0 || 
            strcmp(current_token.lexeme, "const") == 0)) {
        ASTNode *decl_node = declaration_stat();
        if (!decl_node) continue;
        
        ASTNode *temp = decl_node;
        while (temp) {
            if (!root) {
                root = temp;
            } else {
                prev->next = temp;
            }
            prev = temp;
            ASTNode *next_temp = temp->next;
            temp->next = NULL;
            temp = next_temp;
        }
    }

    // 函数定义
    while (current_token.type == TOKEN_KEYWORD && strcmp(current_token.lexeme, "func") == 0) {
        ASTNode *func_node = fun_declaration();
        if (!root) {
            root = func_node;
        } else {
            prev->next = func_node;
        }
        prev = func_node;
    }

    ASTNode *main_node = main_declaration();
    if (!root) {
        root = main_node;
    } else {
        prev->next = main_node;
    }

    Token program_token;
    program_token.type = TOKEN_KEYWORD;
    strcpy(program_token.lexeme, "program");

    ASTNode *node = create_node(program_token);
    node->right = root;

    return node;
}

// ============ 语义分析和代码生成 ============
int lookup_symbol(const char *name, int scope) {
    for (int i = symbol_count - 1; i >= 0; i--) {
        if (strcmp(symbol_table[i].name, name) == 0) {
            if (symbol_table[i].scope <= scope || symbol_table[i].scope == 0) {
                return i;
            }
        }
    }
    return -1;
}

int insert_symbol(const char *name, SymbolKind kind, const char *type, int scope, int array_size) {
    // 检查是否已存在
    for (int i = symbol_count - 1; i >= 0; i--) {
        if (strcmp(symbol_table[i].name, name) == 0 && symbol_table[i].scope == scope) {
            return -1;
        }
    }

    if (symbol_count >= MAX_SYMBOLS) {
        error("Symbol table overflow");
        return -1;
    }

    strcpy(symbol_table[symbol_count].name, name);
    symbol_table[symbol_count].kind = kind;
    strcpy(symbol_table[symbol_count].type, type);
    symbol_table[symbol_count].scope = scope;
    symbol_table[symbol_count].offset = current_offset;
    symbol_table[symbol_count].array_size = array_size;
    symbol_table[symbol_count].init_value = 0;
    symbol_table[symbol_count].has_init = 0;

    // 只有变量和数组需要占用数据内存空间，函数不需要
    if (kind == SYM_ARRAY && array_size > 0) {
        current_offset += array_size;
    } else if (kind == SYM_VAR) {
        current_offset++;
    }

    int result = symbol_count++;
    return result;
}

void enter_scope(void) {
    scope_offsets[current_scope] = current_offset;  // 保存当前作用域的起始 offset
    current_scope++;
    // 不重置 current_offset，让调用者决定
}

void enter_scope_with_params(int param_count) {
    scope_offsets[current_scope] = current_offset;  // 保存当前作用域的起始 offset
    current_scope++;
    current_offset = param_count;  // 函数作用域从参数数量之后开始，避免与参数冲突
}

void exit_scope(void) {
    while (symbol_count > 0 && symbol_table[symbol_count - 1].scope == current_scope) {
        symbol_count--;
    }
    current_scope--;
    current_offset = scope_offsets[current_scope];  // 恢复到进入作用域之前的 offset
}

void generate_code(const char *op, const char *arg, int line) {
    if (code_count >= MAX_CODE_LINES) {
        error("Code buffer overflow");
        return;
    }
    strcpy(code[code_count].op, op);
    strcpy(code[code_count].arg, arg);
    code[code_count].line_no = line;
    code_count++;
}

char* new_label(const char *prefix) {
    static char labels[MAX_CODE_LINES][MAX_IDENT_LEN];
    if (label_count >= MAX_CODE_LINES) {
        error("Label overflow");
        return "ERROR_LABEL";
    }
    sprintf(labels[label_count], "%s%d", prefix, label_count);
    return labels[label_count++];
}

int find_label(const char *label) {
    for (int i = 0; i < code_count; i++) {
        if (strcmp(code[i].op, label) == 0 && strcmp(code[i].arg, ":") == 0) {
            return i;
        }
    }
    return -1;
}

int find_function(const char *name) {
    for (int i = 0; i < func_count; i++) {
        if (strcmp(func_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void push_return_address(int addr) {
    if (return_ptr >= 50) {
        fprintf(stderr, "VM Error: Return address stack overflow\n");
        exit(1);
    }
    return_address_stack[return_ptr++] = addr;
}

int pop_return_address(void) {
    if (return_ptr <= 0) {
        fprintf(stderr, "VM Error: Return address stack underflow\n");
        exit(1);
    }
    return return_address_stack[--return_ptr];
}

void process_node(ASTNode *node) {
    if (!node) return;

    ASTNode *next_node = node->next;

    Token tok = node->token;

    if (tok.type == TOKEN_KEYWORD) {
        if (strcmp(tok.lexeme, "program") == 0) {
            // 不进入scope，让全局变量在scope 0中
            // 首先生成跳转到 main 函数的指令，确保程序从 main 开始执行
            generate_code("BR", "_func_main", tok.line_no);

            // 然后处理所有子节点（全局变量声明和函数定义）
            // 使用循环处理所有子节点，以更好地处理错误恢复
            ASTNode *child = node->right;
            while (child) {
                int prev_err = error_count;
                process_node(child);
                // 如果处理这个节点时产生了错误，下一个节点可能已经通过 next 处理了
                // 但为了安全，我们继续处理后续的 next 节点
                child = child->next;
            }
            // 注意：这里不处理 next_node，因为子节点已经通过 child->next 处理了
            // 并且我们已经添加了全局的 next_node 处理，所以需要提前返回避免重复
            return;
        } else if (strcmp(tok.lexeme, "func") == 0) {
            // 在处理第一个函数之前，设置 next_func_offset 为当前的 current_offset
            // 这样可以确保函数的局部变量从全局变量之后开始
            if (next_func_offset == 0 && current_offset > 0) {
                next_func_offset = current_offset;
            }

            // 记录处理函数前的错误状态
            int prev_error_count = error_count;

            if (node->left && node->left->token.type == TOKEN_IDENTIFIER) {
                const char *func_name = node->left->token.lexeme;
                const char *return_type = "void";
                if (node->left->right) {
                    return_type = node->left->right->token.lexeme;
                }

                strcpy(func_table[func_count].name, func_name);
                strcpy(func_table[func_count].return_type, return_type);
                func_table[func_count].param_count = 0;
                func_table[func_count].code_start = code_count;
                func_table[func_count].local_var_count = 0;

                ASTNode *params = node->mid;
                while (params) {
                    if (params->token.type == TOKEN_IDENTIFIER && params->right) {
                        strcpy(func_table[func_count].params[func_table[func_count].param_count].name, params->token.lexeme);
                        strcpy(func_table[func_count].params[func_table[func_count].param_count].type, params->right->token.lexeme);
                        func_table[func_count].params[func_table[func_count].param_count].offset = func_table[func_count].param_count;
                        func_table[func_count].param_count++;
                    }
                    params = params->next;
                }

                insert_symbol(func_name, SYM_FUNC, return_type, 0, 0);
                func_count++;

                // 生成函数标签
                char func_label[MAX_IDENT_LEN];
                sprintf(func_label, "_func_%s", func_name);
                generate_code(func_label, ":", tok.line_no);

                // 计算参数数量
                int param_count = 0;
                params = node->mid;
                while (params) {
                    if (params->token.type == TOKEN_IDENTIFIER && params->right) {
                        param_count++;
                    }
                    params = params->next;
                }

                enter_scope();  // 先进入作用域，不设置 current_offset
                
                // 为函数分配独立的 offset 空间
                current_offset = next_func_offset;

                // 先按顺序插入参数到符号表（offset 0, 1, 2...）
                params = node->mid;
                int param_offset = current_offset;  // 从函数的起始 offset 开始
                while (params) {
                    if (params->token.type == TOKEN_IDENTIFIER && params->right) {
                        int idx = insert_symbol(params->token.lexeme, SYM_VAR, params->right->token.lexeme, current_scope, 0);
                        if (idx >= 0) {
                            // 确保参数按顺序分配 offset
                            symbol_table[idx].offset = param_offset;
                            param_offset++;
                        }
                    }
                    params = params->next;
                }

                // 在函数入口处生成 ENTER 指令（包含参数数量）
                char enter_arg[20];
                sprintf(enter_arg, "%d", param_count);
                generate_code("ENTER", enter_arg, tok.line_no);

                // 在函数入口处生成参数传递指令：将栈中的参数复制到 data_memory
                // 注意：栈上的参数顺序是 [第一个参数, 第二个参数, ...]
                // 弹出顺序是先第二个参数，后第一个参数
                
                // 然后反向生成 STO 指令（从最后一个参数开始）
                // 先计算参数数量并收集参数信息
                ASTNode *param_list[20];
                int param_idx = 0;
                params = node->mid;
                while (params) {
                    if (params->token.type == TOKEN_IDENTIFIER && params->right) {
                        param_list[param_idx++] = params;
                    }
                    params = params->next;
                }
                
                // 反向生成 STO 指令：从最后一个参数开始
                for (int i = param_idx - 1; i >= 0; i--) {
                    params = param_list[i];
                    // 查找参数在符号表中的索引
                    int idx = lookup_symbol(params->token.lexeme, current_scope);
                    if (idx >= 0) {
                        char offset_str[20];
                        sprintf(offset_str, "%d", symbol_table[idx].offset);
                        generate_code("STO", offset_str, tok.line_no);
                    }
                }

                // 现在设置 current_offset 为参数之后的 offset，为局部变量准备
                current_offset = param_offset;

                // 先处理声明列表（变量声明）
                if (node->right->left) {
                    process_node(node->right->left);
                }
                // 然后处理语句列表
                if (node->right->right) {
                    process_node(node->right->right);
                }

                if (strcmp(return_type, "void") == 0) {
                    generate_code("RETURN", "", tok.line_no);
                }

                // 更新 next_func_offset，为下一个函数准备独立的 offset 空间
                next_func_offset = current_offset;

                exit_scope();

                // 如果在处理这个函数时产生了错误，记录但不停止
                // 这样后续函数仍能被解析
                if (error_count > prev_error_count) {
                    // 函数内部有错误，但已经尝试恢复
                    // 这里可以添加额外的日志或处理
                }
                return;  // 避免重复处理 next_node
            }
        } else if (strcmp(tok.lexeme, "main") == 0) {
            // 记录处理 main 前的错误状态
            int prev_main_err = error_count;
            insert_symbol("main", SYM_FUNC, "void", 0, 0);

            // 生成 main 函数标签
            generate_code("_func_main", ":", tok.line_no);

            // 为 main 函数生成 ENTER 指令（main 函数没有参数）
            generate_code("ENTER", "0", tok.line_no);

            enter_scope();

            // 为 main 函数分配独立的 offset 空间
            current_offset = next_func_offset;

            // 在 main 函数开始时，为所有全局变量生成初始化代码
            for (int i = 0; i < symbol_count; i++) {
                if (symbol_table[i].scope == 0 &&
                    symbol_table[i].kind == SYM_VAR &&
                    symbol_table[i].has_init) {
                    // 生成初始化代码
                    char arg[MAX_IDENT_LEN];
                    sprintf(arg, "%d", symbol_table[i].init_value);
                    generate_code("LOADI", arg, tok.line_no);
                    sprintf(arg, "%d", symbol_table[i].offset + GLOBAL_BASE);
                    generate_code("STO", arg, tok.line_no);
                }
            }

            // 先处理声明列表（变量声明）
            if (node->right->left) {
                process_node(node->right->left);
            }
            // 然后处理语句列表
            if (node->right->right) {
                process_node(node->right->right);
            }
            
            // 更新 next_func_offset
            next_func_offset = current_offset;
            
            exit_scope();

            generate_code("STOP", "", tok.line_no);
            return;  // 避免重复处理 next_node
        } else if (strcmp(tok.lexeme, "var") == 0 || strcmp(tok.lexeme, "let") == 0 || strcmp(tok.lexeme, "const") == 0) {
            if (node->left && node->left->token.type == TOKEN_IDENTIFIER) {
                const char *name = node->left->token.lexeme;
                const char *type = "int";
                if (node->right) {
                    type = node->right->token.lexeme;
                }
                int array_size = 0;
                // 使用 is_array 字段来判断是否是数组
                if (node->is_array && node->mid && node->mid->token.type == TOKEN_NUM) {
                    array_size = node->mid->token.int_value;
                }

                int idx;
                if (node->is_array) {
                    idx = insert_symbol(name, SYM_ARRAY, type, current_scope, array_size);
                } else {
                    idx = insert_symbol(name, SYM_VAR, type, current_scope, 0);
                }

                if (idx == -1) {
                    // 使用正确的行号（来自 AST 节点）
                    char msg[100];
                    sprintf(msg, "Variable already declared");
                    fprintf(stderr, "Error %d: %s at line %d\n", error_count + 1, msg, tok.line_no);
                    error_count++;
                    has_errors = true;
                    return;
                }

                // 如果是全局变量且有初始化值，保存到符号表中
                if (current_scope == 0 && node->mid && !node->is_array) {
                    // 检查初始化值是否是数字
                    if (node->mid->token.type == TOKEN_NUM) {
                        symbol_table[idx].init_value = node->mid->token.int_value;
                        symbol_table[idx].has_init = 1;
                    }
                    // 如果初始化值不是数字，不处理（复杂表达式初始化）
                }
                
                // 如果是局部变量且有初始化值，生成初始化代码
                if (current_scope != 0 && node->mid) {
                    process_node(node->mid);
                    int idx = lookup_symbol(name, current_scope);
                    if (idx != -1) {
                        char arg[MAX_IDENT_LEN];
                        sprintf(arg, "%d", symbol_table[idx].offset);
                        generate_code("STO", arg, tok.line_no);
                    }
                }
            }
        } else if (strcmp(tok.lexeme, "if") == 0) {
            process_node(node->left);
            char *else_label = new_label("ELSE");
            generate_code("BRF", else_label, tok.line_no);
            process_node(node->mid);
            if (node->right) {
                char *end_label = new_label("ENDIF");
                generate_code("BR", end_label, tok.line_no);
                generate_code(else_label, ":", tok.line_no);
                process_node(node->right);
                generate_code(end_label, ":", tok.line_no);
            } else {
                generate_code(else_label, ":", tok.line_no);
            }
        } else if (strcmp(tok.lexeme, "while") == 0) {
            char *loop_label = new_label("WHILE");
            char *end_label = new_label("ENDWHILE");
            generate_code(loop_label, ":", tok.line_no);
            process_node(node->left);
            generate_code("BRF", end_label, tok.line_no);
            enter_scope();
            process_node(node->right);
            exit_scope();
            generate_code("BR", loop_label, tok.line_no);
            generate_code(end_label, ":", tok.line_no);
        } else if (strcmp(tok.lexeme, "do") == 0) {
            char *loop_label = new_label("DO");
            char *end_label = new_label("ENDDO");
            generate_code(loop_label, ":", tok.line_no);
            enter_scope();
            process_node(node->left);
            exit_scope();
            process_node(node->right);
            generate_code("BRF", end_label, tok.line_no);
            generate_code("BR", loop_label, tok.line_no);
            generate_code(end_label, ":", tok.line_no);
        } else if (strcmp(tok.lexeme, "for") == 0) {
            enter_scope();
            if (node->left && node->left->token.type == TOKEN_IDENTIFIER) {
                insert_symbol(node->left->token.lexeme, SYM_VAR, "int", current_scope, 0);
            }
            process_node(node->mid);
            process_node(node->right);
            exit_scope();
        } else if (strcmp(tok.lexeme, "switch") == 0) {
            process_node(node->left);
            generate_code("STO", "SWITCH_VAL", tok.line_no);

            ASTNode *cases = node->right;
            char *default_label = new_label("DEFAULT");
            char *end_switch_label = new_label("ENDSWITCH");

            while (cases) {
                if (strcmp(cases->token.lexeme, "case") == 0) {
                    generate_code("LOAD", "SWITCH_VAL", tok.line_no);
                    char case_val[20];
                    sprintf(case_val, "%d", cases->left->token.int_value);
                    generate_code("LOADI", case_val, tok.line_no);
                    generate_code("EQ", "", tok.line_no);
                    char *case_label = new_label("CASE");
                    generate_code("BRF", case_label, tok.line_no);
                    process_node(cases->right);
                    generate_code("BR", end_switch_label, tok.line_no);
                    generate_code(case_label, ":", tok.line_no);
                } else if (strcmp(cases->token.lexeme, "default") == 0) {
                    generate_code("BR", default_label, tok.line_no);
                }
                cases = cases->next;
            }

            generate_code(default_label, ":", tok.line_no);
            // 处理 default 分支的语句体
            // 需要重新遍历找到 default 分支
            cases = node->right;
            while (cases) {
                if (strcmp(cases->token.lexeme, "default") == 0) {
                    process_node(cases->right);
                    break;
                }
                cases = cases->next;
            }
            generate_code("BR", end_switch_label, tok.line_no);
            generate_code(end_switch_label, ":", tok.line_no);
        } else if (strcmp(tok.lexeme, "read") == 0) {
            if (node->left && node->left->token.type == TOKEN_IDENTIFIER) {
                ASTNode *arr_index = node->left->left;
                if (arr_index) {
                    int idx = lookup_symbol(node->left->token.lexeme, current_scope);
                    if (idx == -1) {
                        error("Undefined variable");
                    } else {
                        generate_code("IN", "", tok.line_no);
                        process_node(arr_index);
                        int base_offset = symbol_table[idx].offset;
                        generate_code("LOADI", "", tok.line_no);
                        char base_str[20];
                        sprintf(base_str, "%d", base_offset);
                        generate_code("LOADI", base_str, tok.line_no);
                        generate_code("ADD", "", tok.line_no);
                        generate_code("STOIDX", "", tok.line_no);
                    }
                } else {
                    int idx = lookup_symbol(node->left->token.lexeme, current_scope);
                    if (idx == -1) {
                        error("Undefined variable");
                    } else {
                        generate_code("IN", "", tok.line_no);
                        char arg[MAX_IDENT_LEN];
                        sprintf(arg, "%d", symbol_table[idx].offset);
                        generate_code("STO", arg, tok.line_no);
                    }
                }
            }
        } else if (strcmp(tok.lexeme, "write") == 0) {
            process_node(node->left);
            generate_code("OUT", "", tok.line_no);
        } else if (strcmp(tok.lexeme, "return") == 0) {
            if (node->left) {
                process_node(node->left);
            }
            generate_code("RETURN", "", tok.line_no);
        } else if (strcmp(tok.lexeme, "break") == 0) {
            generate_code("BR", "BREAK", tok.line_no);
        } else if (strcmp(tok.lexeme, "continue") == 0) {
            generate_code("BR", "CONTINUE", tok.line_no);
        }
    } else if (tok.type == TOKEN_DELIMITER && tok.lexeme[0] == '{') {
        enter_scope();
        process_node(node->left);
        process_node(node->right);
        exit_scope();
    } else if (tok.type == TOKEN_OPERATOR) {
        const char *op = tok.lexeme;
        if (strcmp(op, "=") == 0) {
            if (node->left && node->left->token.type == TOKEN_IDENTIFIER) {
                ASTNode *arr_index = node->left->left;
                if (arr_index) {
                    int idx = lookup_symbol(node->left->token.lexeme, current_scope);
                    if (idx == -1) {
                        fprintf(stderr, "Error %d: Undefined variable at line %d\n", error_count + 1, tok.line_no);
                        error_count++;
                        has_errors = true;
                        return;
                    }
                    process_node(node->right);
                    process_node(arr_index);
                    int base_offset = symbol_table[idx].offset;
                    // 如果是全局变量，加上全局变量基址
                    if (symbol_table[idx].scope == 0) {
                        base_offset += GLOBAL_BASE;
                    }
                    char base_str[20];
                    sprintf(base_str, "%d", base_offset);
                    generate_code("LOADI", base_str, tok.line_no);
                    generate_code("ADD", "", tok.line_no);
                    generate_code("STOIDX", "", tok.line_no);
                } else {
                    int idx = lookup_symbol(node->left->token.lexeme, current_scope);
                    if (idx == -1) {
                        fprintf(stderr, "Error %d: Undefined variable at line %d\n", error_count + 1, tok.line_no);
                        error_count++;
                        has_errors = true;
                        return;
                    }
                    process_node(node->right);
                    char arg[MAX_IDENT_LEN];
                    int addr = symbol_table[idx].offset;
                    // 如果是全局变量，加上全局变量基址
                    if (symbol_table[idx].scope == 0) {
                        addr += GLOBAL_BASE;
                    }
                    sprintf(arg, "%d", addr);
                    generate_code("STO", arg, tok.line_no);
                }
            }
        } else {
            process_node(node->left);
            process_node(node->right);
            if (strcmp(op, "+") == 0) generate_code("ADD", "", tok.line_no);
            else if (strcmp(op, "-") == 0) generate_code("SUB", "", tok.line_no);
            else if (strcmp(op, "*") == 0) generate_code("MULT", "", tok.line_no);
            else if (strcmp(op, "/") == 0) generate_code("DIV", "", tok.line_no);
            else if (strcmp(op, "%") == 0) generate_code("MOD", "", tok.line_no);
            else if (strcmp(op, "==") == 0) generate_code("EQ", "", tok.line_no);
            else if (strcmp(op, "!=") == 0) generate_code("NOTEQ", "", tok.line_no);
            else if (strcmp(op, ">") == 0) generate_code("GT", "", tok.line_no);
            else if (strcmp(op, "<") == 0) generate_code("LES", "", tok.line_no);
            else if (strcmp(op, ">=") == 0) generate_code("GE", "", tok.line_no);
            else if (strcmp(op, "<=") == 0) generate_code("LE", "", tok.line_no);
            else if (strcmp(op, "&&") == 0) generate_code("AND", "", tok.line_no);
            else if (strcmp(op, "||") == 0) generate_code("OR", "", tok.line_no);
        }
    } else if (tok.type == TOKEN_IDENTIFIER) {
        ASTNode *arr_index = node->left;
        ASTNode *args = node->mid;

        if (arr_index) {
            int idx = lookup_symbol(tok.lexeme, current_scope);
            if (idx == -1) {
                fprintf(stderr, "Error %d: Undefined variable at line %d\n", error_count + 1, tok.line_no);
                error_count++;
                has_errors = true;
            } else {
                process_node(arr_index);
                int base_offset = symbol_table[idx].offset;
                // 如果是全局变量，加上全局变量基址
                if (symbol_table[idx].scope == 0) {
                    base_offset += GLOBAL_BASE;
                }
                char base_str[20];
                sprintf(base_str, "%d", base_offset);
                generate_code("LOADI", base_str, tok.line_no);
                generate_code("ADD", "", tok.line_no);
                generate_code("LOADIDX", "", tok.line_no);
            }
        } else {
            // 检查是否是函数调用（包括无参数函数）
            int func_idx = find_function(tok.lexeme);
            
            // 如果有参数列表，说明是函数调用
            if (args || func_idx != -1) {
                // 是函数调用
                if (func_idx == -1) {
                    // 函数未定义
                    fprintf(stderr, "Error %d: Undefined function at line %d\n", error_count + 1, tok.line_no);
                    error_count++;
                    has_errors = true;
                } else {
                    // 函数已定义，生成调用代码
                    if (args) {
                        process_node(args);
                    }

                    char target_label[MAX_IDENT_LEN];
                    sprintf(target_label, "_func_%s", tok.lexeme);
                    generate_code("CAL", target_label, tok.line_no);
                }
            } else {
                // 是普通变量访问
                int idx = lookup_symbol(tok.lexeme, current_scope);
                if (idx == -1) {
                    fprintf(stderr, "Error %d: Undefined variable at line %d\n", error_count + 1, tok.line_no);
                    error_count++;
                    has_errors = true;
                } else {
                    char arg[MAX_IDENT_LEN];
                    int addr = symbol_table[idx].offset;
                    // 如果是全局变量，加上全局变量基址
                    if (symbol_table[idx].scope == 0) {
                        addr += GLOBAL_BASE;
                    }
                    sprintf(arg, "%d", addr);
                    generate_code("LOAD", arg, tok.line_no);
                }
            }
        }
    } else if (tok.type == TOKEN_NUM) {
        generate_code("LOADI", tok.lexeme, tok.line_no);
    }

    // 处理 next 节点（用于链表结构，如语句列表）
    // 只在函数内部（current_scope > 0）且不在全局变量声明中处理
    // 全局变量已经通过 program 节点的循环处理了
    if (next_node && current_scope > 0) {
        process_node(next_node);
    }
}

// ============ 虚拟机执行 ============
void vm_push(int value) {
    if (stack_ptr >= STACK_SIZE) {
        fprintf(stderr, "VM Error: Stack overflow\n");
        exit(1);
    }
    operand_stack[stack_ptr++] = value;
}

int vm_pop(void) {
    if (stack_ptr <= 0) {
        fprintf(stderr, "VM Error: Stack underflow\n");
        exit(1);
    }
    return operand_stack[--stack_ptr];
}

void vm_execute(void) {
    program_counter = 0;
    stack_ptr = 0;
    
    printf("\n===== VM Execution Output =====\n");
    
    while (program_counter < code_count) {
        CodeLine *instr = &code[program_counter];
        
        if (strcmp(instr->arg, ":") == 0) {
            program_counter++;
            continue;
        }

        if (strcmp(instr->op, "LOAD") == 0) {
            int addr = atoi(instr->arg);
            vm_push(data_memory[addr]);
        } else if (strcmp(instr->op, "LOADI") == 0) {
            vm_push(atoi(instr->arg));
        } else if (strcmp(instr->op, "STO") == 0) {
            int addr = atoi(instr->arg);
            data_memory[addr] = vm_pop();
        } else if (strcmp(instr->op, "ADD") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a + b);
        } else if (strcmp(instr->op, "SUB") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a - b);
        } else if (strcmp(instr->op, "MULT") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a * b);
        } else if (strcmp(instr->op, "DIV") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            if (b == 0) {
                fprintf(stderr, "VM Error: Division by zero\n");
                exit(1);
            }
            vm_push(a / b);
        } else if (strcmp(instr->op, "MOD") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            if (b == 0) {
                fprintf(stderr, "VM Error: Division by zero\n");
                exit(1);
            }
            vm_push(a % b);
        } else if (strcmp(instr->op, "BR") == 0) {
            int target = find_label(instr->arg);
            if (target == -1) {
                fprintf(stderr, "VM Error: Label '%s' not found\n", instr->arg);
                exit(1);
            }
            program_counter = target;
            continue;
        } else if (strcmp(instr->op, "BRF") == 0) {
            int cond = vm_pop();
            if (cond == 0) {
                int target = find_label(instr->arg);
                if (target == -1) {
                    fprintf(stderr, "VM Error: Label '%s' not found\n", instr->arg);
                    exit(1);
                }
                program_counter = target;
                continue;
            }
        } else if (strcmp(instr->op, "EQ") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a == b ? 1 : 0);
        } else if (strcmp(instr->op, "NOTEQ") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a != b ? 1 : 0);
        } else if (strcmp(instr->op, "GT") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a > b ? 1 : 0);
        } else if (strcmp(instr->op, "LES") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a < b ? 1 : 0);
        } else if (strcmp(instr->op, "GE") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a >= b ? 1 : 0);
        } else if (strcmp(instr->op, "LE") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push(a <= b ? 1 : 0);
        } else if (strcmp(instr->op, "AND") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push((a != 0 && b != 0) ? 1 : 0);
        } else if (strcmp(instr->op, "OR") == 0) {
            int b = vm_pop();
            int a = vm_pop();
            vm_push((a != 0 || b != 0) ? 1 : 0);
        } else if (strcmp(instr->op, "IN") == 0) {
            int value;
            scanf("%d", &value);
            vm_push(value);
        } else if (strcmp(instr->op, "OUT") == 0) {
            int value = vm_pop();
            printf("%d\n", value);
        } else if (strcmp(instr->op, "CAL") == 0) {
            int target = find_label(instr->arg);
            if (target == -1) {
                fprintf(stderr, "VM Error: Function '%s' not found\n", instr->arg);
                exit(1);
            }
            push_return_address(program_counter + 1);
            program_counter = target;
            continue;
        } else if (strcmp(instr->op, "ENTER") == 0) {
            // ENTER n - 建立栈帧，参数 n 表示参数数量
            // 在当前实现中，我们使用静态内存分配，所以这里不需要做太多事情
            // 但为了符合标准的栈帧机制，我们可以记录参数数量
            int param_count = atoi(instr->arg);
            (void)param_count;  // 暂时不需要使用这个值
            // 在更完善的实现中，这里会保存基址指针并分配局部变量空间
        } else if (strcmp(instr->op, "RETURN") == 0) {
            if (return_ptr > 0) {
                program_counter = pop_return_address();
            } else {
                break;
            }
            continue;
        } else if (strcmp(instr->op, "STOP") == 0) {
            break;
        } else if (strcmp(instr->op, "LOADIDX") == 0) {
            int addr = vm_pop();
            vm_push(data_memory[addr]);
        } else if (strcmp(instr->op, "STOIDX") == 0) {
            int addr = vm_pop();
            int value = vm_pop();
            data_memory[addr] = value;
        } else {
            fprintf(stderr, "VM Error: Unknown instruction '%s'\n", instr->op);
            exit(1);
        }
        
        program_counter++;
    }
    
    printf("===== Execution Complete =====\n");
}

void print_code(FILE *out) {
    fprintf(out, "===== Intermediate Code =====\n");
    fprintf(out, "%-4s %-12s %-15s %s\n", "Line", "Op", "Arg", "Source Line");
    fprintf(out, "----------------------------------------\n");
    
    for (int i = 0; i < code_count; i++) {
        fprintf(out, "%-4d %-12s %-15s %d\n", i + 1, code[i].op, code[i].arg, code[i].line_no);
    }
    
    fprintf(out, "\nTotal instructions: %d\n", code_count);
}

void print_symbol_table(FILE *out) {
    fprintf(out, "===== Symbol Table =====\n");
    fprintf(out, "%-15s %-8s %-8s %-6s %-6s %-8s\n", "Name", "Kind", "Type", "Scope", "Offset", "ArraySize");
    fprintf(out, "------------------------------------------------\n");
    
    for (int i = 0; i < symbol_count; i++) {
        const char *kind_str = "VAR";
        if (symbol_table[i].kind == SYM_FUNC) kind_str = "FUNC";
        else if (symbol_table[i].kind == SYM_ARRAY) kind_str = "ARRAY";
        fprintf(out, "%-15s %-8s %-8s %-6d %-6d %-8d\n",
                symbol_table[i].name,
                kind_str,
                symbol_table[i].type,
                symbol_table[i].scope,
                symbol_table[i].offset,
                symbol_table[i].array_size);
    }
    
    fprintf(out, "\nTotal symbols: %d\n", symbol_count);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source_file>\n", argv[0]);
        return 1;
    }

    source_file = fopen(argv[1], "r");
    if (!source_file) {
        fprintf(stderr, "Error: cannot open source file '%s'\n", argv[1]);
        return 1;
    }

    printf("===== Lexical Analysis =====\n");
    get_char();
    next_token();  // 获取第一个 token
    
    // 词法分析阶段：扫描整个文件，保存token到缓冲区
    token_count = 0;
    int prev_error_count = error_count;
    while (current_token.type != TOKEN_EOF) {
        // 保存token到缓冲区
        if (token_count < MAX_TOKENS) {
            token_buffer[token_count] = current_token;
            token_count++;
        }
        next_token();
    }
    // 保存EOF token
    if (token_count < MAX_TOKENS) {
        token_buffer[token_count] = current_token;
        token_count++;
    }
    
    // 打印词法错误
    print_lex_errors();
    
    // 如果有词法错误，显示错误数量
    if (error_count > prev_error_count) {
        printf("Lexical errors found: %d\n", error_count - prev_error_count);
    }
    
    // 关闭文件，因为我们已经有了所有token
    fclose(source_file);
    source_file = NULL;

    printf("\n===== Parsing =====\n");
    // 重置错误计数，因为词法错误已经报告过了
    // 但保留 has_errors 标志
    prev_error_count = error_count;
    
    // 从缓冲区读取token进行语法分析
    token_pos = 0;
    next_token();  // 获取第一个 token
    ASTNode *ast = program();

    // 即使有错误，也继续进行语义分析和代码生成，以检测更多错误
    printf("\n===== Semantic Analysis and Code Generation =====\n");
    // 不调用 enter_scope()，让全局变量在 scope 0 中处理
    process_node(ast);

    if (has_errors) {
        printf("\n===== Error Summary =====\n");
        printf("Total errors found: %d\n", error_count > MAX_ERRORS ? MAX_ERRORS : error_count);
        fclose(source_file);
        return 1;
    }

    FILE *code_file = fopen("output_code.txt", "w");
    if (code_file) {
        print_code(code_file);
        fclose(code_file);
        printf("Intermediate code saved to 'output_code.txt'\n");
    }

    FILE *symbol_file = fopen("output_symbols.txt", "w");
    if (symbol_file) {
        print_symbol_table(symbol_file);
        fclose(symbol_file);
        printf("Symbol table saved to 'output_symbols.txt'\n");
    }

    vm_execute();

    fclose(source_file);
    
    for (int i = 0; i < ast_node_count; i++) {
        free(ast_nodes[i]);
    }

    return 0;
}
