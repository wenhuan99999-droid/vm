#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <ctype.h> 
#include <stdbool.h> 

#define MAX_IDENT_LEN 32 
#define MAX_TOKEN_LEN 64 

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

static Token current_token; 
static FILE *token_file = NULL; 
static bool has_more_tokens = true; 

// ============ 错误处理全局变量 ============
static int error_count = 0; 
static bool has_errors = false; 
#define MAX_ERRORS 20 

// ============ AST 定义 ============

typedef struct ASTNode { 
    Token token; 
    struct ASTNode *left; 
    struct ASTNode *right; 
    struct ASTNode *mid; 
    struct ASTNode *next; 
} ASTNode; 

ASTNode* create_node(Token token) { 
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
    return node; 
} 

// ============ 函数声明 ============
void next_token(void);

// ============ 错误处理函数 ============

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
    // 跳过直到找到同步点：分号、右括号、右花括号、文件结束、关键字
    while (current_token.type != TOKEN_EOF) { 
        if (current_token.type == TOKEN_DELIMITER) { 
            switch (current_token.lexeme[0]) { 
                case ';': 
                case ')': 
                case '}': 
                    return; 
            } 
        } 
        // 遇到关键字也作为同步点
        if (current_token.type == TOKEN_KEYWORD) { 
            const char *keywords[] = {"var", "let", "const", "if", "while", "for", "read", "write", "return", "break", "continue", "main"}; 
            for (int i = 0; i < 12; i++) { 
                if (strcmp(current_token.lexeme, keywords[i]) == 0) { 
                    return; 
                } 
            } 
        } 
        // 单独的分号也是同步点（但会报告错误）
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') { 
            return; 
        } 
        next_token(); 
        // 防止无限循环
        static int skip_count = 0; 
        if (skip_count++ > 1000) { 
            fprintf(stderr, "Warning: skipping too many tokens, breaking loop\n"); 
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
    } 
} 

// ============ 从文件读取 Token ============

TokenType string_to_token_type(const char *str) { 
    if (strcmp(str, "KEYWORD") == 0) return TOKEN_KEYWORD; 
    if (strcmp(str, "IDENT") == 0) return TOKEN_IDENTIFIER; 
    if (strcmp(str, "NUM") == 0) return TOKEN_NUM; 
    if (strcmp(str, "OP") == 0) return TOKEN_OPERATOR; 
    if (strcmp(str, "DELIM") == 0) return TOKEN_DELIMITER; 
    if (strcmp(str, "ERROR") == 0) return TOKEN_ERROR; 
    if (strcmp(str, "EOF") == 0) return TOKEN_EOF; 
    return TOKEN_ERROR; 
} 

void next_token(void) { 
    if (!has_more_tokens) { 
        current_token.type = TOKEN_EOF; 
        strcpy(current_token.lexeme, "EOF"); 
        return; 
    } 

    char type_str[20]; 
    char lexeme[MAX_TOKEN_LEN]; 
    int line_no; 
    int int_value = 0; 

    // 跳过空行和注释行 
    char line[256]; 
    while (fgets(line, sizeof(line), token_file)) { 
        // 跳过空行 
        if (line[0] == '\n' || line[0] == '\r') continue; 
        // 跳过注释行 
        if (strncmp(line, "=====", 5) == 0 || strncmp(line, "------", 6) == 0 || 
            strncmp(line, "Type", 4) == 0 || strncmp(line, "Total", 5) == 0) { 
            continue; 
        } 
        break; 
    } 

    if (feof(token_file)) { 
        has_more_tokens = false; 
        current_token.type = TOKEN_EOF; 
        strcpy(current_token.lexeme, "EOF"); 
        return; 
    } 

    // 解析 token 行格式: TYPE LEXEME LINE [VALUE] 
    if (sscanf(line, "%19s %63s %d", type_str, lexeme, &line_no) >= 3) { 
        current_token.type = string_to_token_type(type_str); 
        strcpy(current_token.lexeme, lexeme); 
        current_token.line_no = line_no; 

        // 尝试读取数字值 
        char *ptr = line; 
        int count = 0; 
        while (*ptr && count < 3) { 
            if (*ptr == ' ') count++; 
            ptr++; 
        } 
        // 跳过 lexeme 和 line_no 
        while (*ptr && *ptr != ' ') ptr++; 
        while (*ptr == ' ') ptr++; 
        if (*ptr >= '0' && *ptr <= '9') { 
            current_token.int_value = atoi(ptr); 
        } else { 
            current_token.int_value = 0; 
        } 
    } else { 
        has_more_tokens = false; 
        current_token.type = TOKEN_EOF; 
        strcpy(current_token.lexeme, "EOF"); 
    } 
} 

// ============ 语法分析函数 ============

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

ASTNode* program(); 
ASTNode* main_declaration(); 
ASTNode* fun_body(); 
ASTNode* declaration_list(); 
ASTNode* declaration_stat(); 
ASTNode* statement_list(); 
ASTNode* statement(); 
ASTNode* if_stat(); 
ASTNode* while_stat(); 
ASTNode* for_stat(); 
ASTNode* read_stat(); 
ASTNode* write_stat(); 
ASTNode* compound_stat(); 
ASTNode* expression_stat(); 
ASTNode* return_stat(); 
ASTNode* expression(); 
ASTNode* bool_expr(); 
ASTNode* additive_expr(); 
ASTNode* term(); 
ASTNode* factor(); 

ASTNode* factor() { 
    ASTNode *node = NULL; 
    Token token = current_token; 

    if (current_token.type == TOKEN_IDENTIFIER) { 
        next_token(); 
        node = create_node(token); 
    } else if (current_token.type == TOKEN_NUM) { 
        next_token(); 
        node = create_node(token); 
    } else if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '(') { 
        match_delimiter_safe('('); 
        node = additive_expr(); 
        match_delimiter_safe(')'); 
    } else if (current_token.type != TOKEN_EOF) { 
        char msg[100]; 
        sprintf(msg, "unexpected token in factor: '%s'", current_token.lexeme); 
        error(msg); 
        skip_to_sync_point(); 
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '(') { 
            match_delimiter_safe('('); 
            node = additive_expr(); 
            match_delimiter_safe(')'); 
        } 
    } 
    return node; 
} 

ASTNode* term() { 
    ASTNode *left = factor(); 
    Token token; 

    while (current_token.type == TOKEN_OPERATOR && 
           (strcmp(current_token.lexeme, "*") == 0 || strcmp(current_token.lexeme, "/") == 0)) { 
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

ASTNode* expression() { 
    if (current_token.type == TOKEN_IDENTIFIER) { 
        Token id_token = current_token; 
        next_token(); 
        if (current_token.type == TOKEN_OPERATOR && strcmp(current_token.lexeme, "=") == 0) { 
            Token assign_token = current_token; 
            next_token(); 
            ASTNode *expr_node = bool_expr(); 
            ASTNode *node = create_node(assign_token); 
            ASTNode *id_node = create_node(id_token); 
            node->left = id_node; 
            node->right = expr_node; 
            return node; 
        } else { 
            return create_node(id_token); 
        } 
    } 
    return bool_expr(); 
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

    Token id_token = current_token; 
    if (current_token.type != TOKEN_IDENTIFIER) { 
        error("expected identifier"); 
        // 跳过到分号，进行错误恢复
        while (current_token.type != TOKEN_EOF && 
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';')) { 
            next_token(); 
        } 
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') { 
            next_token(); 
        } 
        return NULL; 
    } 
    next_token(); 

    // 检查是否缺少冒号
    if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ':') { 
        error("expected ':'"); 
        // 跳过到分号，进行错误恢复
        while (current_token.type != TOKEN_EOF && 
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';')) { 
            next_token(); 
        } 
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') { 
            next_token(); 
        } 
        // 创建一个简化的声明节点
        ASTNode *node = create_node(kind_token); 
        ASTNode *id_node = create_node(id_token); 
        node->left = id_node; 
        return node; 
    } 
    next_token(); 

    // 检查类型
    if (current_token.type != TOKEN_KEYWORD || strcmp(current_token.lexeme, "int") != 0) { 
        error("expected 'int'"); 
        // 跳过到分号
        while (current_token.type != TOKEN_EOF && 
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';')) { 
            next_token(); 
        } 
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') { 
            next_token(); 
        } 
        ASTNode *node = create_node(kind_token); 
        ASTNode *id_node = create_node(id_token); 
        node->left = id_node; 
        return node; 
    } 
    next_token(); 

    ASTNode *node = create_node(kind_token); 
    ASTNode *id_node = create_node(id_token); 
    Token type_token; 
    type_token.type = TOKEN_KEYWORD; 
    strcpy(type_token.lexeme, "int"); 
    ASTNode *type_node = create_node(type_token); 

    node->left = id_node; 
    node->right = type_node; 

    if (current_token.type == TOKEN_OPERATOR && strcmp(current_token.lexeme, "=") == 0) { 
        next_token(); 
        ASTNode *expr_node = expression(); 
        node->mid = expr_node; 
    } 

    if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') { 
        match_delimiter_safe(';'); 
    } 

    return node; 
} 

ASTNode* declaration_list() { 
    ASTNode *root = NULL; 
    ASTNode *prev = NULL; 

    while (current_token.type == TOKEN_KEYWORD && 
           (strcmp(current_token.lexeme, "var") == 0 || strcmp(current_token.lexeme, "let") == 0 || strcmp(current_token.lexeme, "const") == 0)) { 
        ASTNode *decl_node = declaration_stat(); 

        if (root == NULL) { 
            root = decl_node; 
        } else { 
            prev->next = decl_node; 
        } 
        prev = decl_node; 
    } 

    return root; 
} 

ASTNode* if_stat() { 
    match_keyword_safe("if"); 
    match_delimiter_safe('('); 

    ASTNode *cond = bool_expr(); 

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
    
    // 检查是否缺少左括号
    if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != '(') { 
        error("expected '('"); 
        // 跳过直到找到语句结束（关键字、右花括号或左花括号）
        while (current_token.type != TOKEN_EOF && 
               !(current_token.type == TOKEN_KEYWORD) &&
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}') &&
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '{')) { 
            next_token(); 
        } 
        // 如果找到左花括号，尝试解析复合语句作为循环体
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '{') { 
            ASTNode *body = compound_stat(); 
            Token while_token; 
            while_token.type = TOKEN_KEYWORD; 
            strcpy(while_token.lexeme, "while"); 
            ASTNode *node = create_node(while_token); 
            node->right = body; 
            return node; 
        } 
        // 返回一个空的while节点，继续解析后面的内容
        Token while_token; 
        while_token.type = TOKEN_KEYWORD; 
        strcpy(while_token.lexeme, "while"); 
        return create_node(while_token); 
    } 
    next_token(); 

    ASTNode *cond = bool_expr(); 

    // 检查是否缺少右括号
    if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ')') { 
        error("expected ')'"); 
        // 跳过直到找到语句结束
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
            node->left = cond; 
            node->right = body; 
            return node; 
        } 
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

    ASTNode *expr_node = bool_expr(); 

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

ASTNode* read_stat() { 
    match_keyword_safe("read"); 

    Token id_token = current_token; 
    if (current_token.type != TOKEN_IDENTIFIER) { 
        error("expected identifier"); 
        skip_to_sync_point(); 
        return NULL; 
    } 
    next_token(); 
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
    
    // 检查是否缺少分号
    if (current_token.type != TOKEN_DELIMITER || current_token.lexeme[0] != ';') { 
        error("expected ';'"); 
        // 只跳过到分号，不跳过关键字，确保下一个语句能被处理
        while (current_token.type != TOKEN_EOF && 
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') &&
               !(current_token.type == TOKEN_KEYWORD) &&
               !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}')) { 
            next_token(); 
        } 
        // 不要跳过关键字，让它被下一次statement()调用处理
    } else { 
        next_token(); 
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

    ASTNode *expr_node = expression(); 
    match_delimiter_safe(';'); 

    Token return_token; 
    return_token.type = TOKEN_KEYWORD; 
    strcpy(return_token.lexeme, "return"); 

    ASTNode *node = create_node(return_token); 
    node->left = expr_node; 

    return node; 
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
        } else if (strcmp(current_token.lexeme, "for") == 0) { 
            node = for_stat(); 
        } else if (strcmp(current_token.lexeme, "read") == 0) { 
            node = read_stat(); 
        } else if (strcmp(current_token.lexeme, "write") == 0) { 
            node = write_stat(); 
        } else if (strcmp(current_token.lexeme, "return") == 0) { 
            node = return_stat(); 
        } else if (strcmp(current_token.lexeme, "break") == 0) { 
            next_token(); 
            match_delimiter_safe(';'); 
            Token break_token; 
            break_token.type = TOKEN_KEYWORD; 
            strcpy(break_token.lexeme, "break"); 
            node = create_node(break_token); 
        } else if (strcmp(current_token.lexeme, "continue") == 0) { 
            next_token(); 
            match_delimiter_safe(';'); 
            Token continue_token; 
            continue_token.type = TOKEN_KEYWORD; 
            strcpy(continue_token.lexeme, "continue"); 
            node = create_node(continue_token); 
        } else if (strcmp(current_token.lexeme, "var") == 0 || 
                   strcmp(current_token.lexeme, "let") == 0 || 
                   strcmp(current_token.lexeme, "const") == 0) { 
            // 变量声明应该在声明列表中处理
            char msg[100]; 
            sprintf(msg, "unexpected declaration keyword '%s' in statement context", current_token.lexeme); 
            error(msg); 
            skip_to_sync_point(); 
        } else { 
            // 未知关键字，可能是拼写错误
            char msg[100]; 
            sprintf(msg, "unexpected keyword '%s'", current_token.lexeme); 
            error(msg); 
            skip_to_sync_point(); 
        } 
    } else if (current_token.type == TOKEN_DELIMITER) { 
        if (current_token.lexeme[0] == '{') { 
            node = compound_stat(); 
        } else if (current_token.lexeme[0] == '}') { 
            // 多余的右花括号
            error("unexpected '}'"); 
            next_token(); 
        } else if (current_token.lexeme[0] == ';') { 
            // 多余的分号
            error("unexpected ';'"); 
            next_token(); 
        } else { 
            char msg[100]; 
            sprintf(msg, "unexpected delimiter '%s'", current_token.lexeme); 
            error(msg); 
            next_token(); 
        } 
    } else if (current_token.type == TOKEN_IDENTIFIER || current_token.type == TOKEN_NUM || 
               (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '(')) { 
        node = expression_stat(); 
    } else if (current_token.type == TOKEN_OPERATOR) { 
        // 表达式不能以运算符开头
        char msg[100]; 
        sprintf(msg, "unexpected operator '%s' at statement start", current_token.lexeme); 
        error(msg); 
        skip_to_sync_point(); 
    } else if (current_token.type != TOKEN_EOF) { 
        // 未知 token，报告错误并跳过
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
        // 检查是否到达复合语句结束
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}') { 
            break; 
        } 

        // 检测多余的右花括号
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == '}') { 
            error("unexpected '}'"); 
            next_token(); 
            continue; 
        } 

        // 检测多余的分号
        if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') { 
            error("unexpected ';'"); 
            next_token(); 
            continue; 
        } 

        // 检测表达式以运算符开头
        if (current_token.type == TOKEN_OPERATOR) { 
            char msg[100]; 
            sprintf(msg, "unexpected operator '%s' at statement start", current_token.lexeme); 
            error(msg); 
            // 跳过到下一个分号或关键字
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

        // 检测变量声明在语句位置
        if (current_token.type == TOKEN_KEYWORD && 
            (strcmp(current_token.lexeme, "var") == 0 || 
             strcmp(current_token.lexeme, "let") == 0 || 
             strcmp(current_token.lexeme, "const") == 0)) { 
            // 检查前面是否已经有声明（通过检查是否有语句）
            if (root != NULL) { 
                char msg[100]; 
                sprintf(msg, "unexpected declaration keyword '%s' in statement context", current_token.lexeme); 
                error(msg); 
                // 跳过这个声明
                while (current_token.type != TOKEN_EOF && 
                       !(current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';')) { 
                    next_token(); 
                } 
                if (current_token.type == TOKEN_DELIMITER && current_token.lexeme[0] == ';') { 
                    next_token(); 
                } 
                continue; 
            } 
        } 

        ASTNode *stmt_node = statement(); 

        // 跳过 NULL 节点（错误恢复时可能返回 NULL）
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
    ASTNode *main_node = main_declaration(); 

    Token program_token; 
    program_token.type = TOKEN_KEYWORD; 
    strcpy(program_token.lexeme, "program"); 

    ASTNode *node = create_node(program_token); 
    node->right = main_node; 

    return node; 
} 

// ============ 打印和保存 AST ============

void print_tree(FILE *out, ASTNode *node, int depth) { 
    if (!node) return; 

    for (int i = 0; i < depth; i++) { 
        fprintf(out, "  "); 
    } 

    switch (node->token.type) { 
        case TOKEN_IDENTIFIER: 
            fprintf(out, "Identifier: %s\n", node->token.lexeme); 
            break; 
        case TOKEN_NUM: 
            fprintf(out, "Number: %s\n", node->token.lexeme); 
            break; 
        case TOKEN_OPERATOR: 
            fprintf(out, "Operator: %s\n", node->token.lexeme); 
            break; 
        case TOKEN_KEYWORD: 
            if (strcmp(node->token.lexeme, "program") == 0) { 
                fprintf(out, "Program\n"); 
            } else if (strcmp(node->token.lexeme, "main") == 0) { 
                fprintf(out, "Main Function\n"); 
            } else if (strcmp(node->token.lexeme, "var") == 0) { 
                fprintf(out, "Variable Declaration\n"); 
            } else if (strcmp(node->token.lexeme, "let") == 0) { 
                fprintf(out, "Let Declaration\n"); 
            } else if (strcmp(node->token.lexeme, "const") == 0) { 
                fprintf(out, "Constant Declaration\n"); 
            } else if (strcmp(node->token.lexeme, "if") == 0) { 
                fprintf(out, "If Statement\n"); 
            } else if (strcmp(node->token.lexeme, "else") == 0) { 
                fprintf(out, "Else Clause\n"); 
            } else if (strcmp(node->token.lexeme, "while") == 0) { 
                fprintf(out, "While Loop\n"); 
            } else if (strcmp(node->token.lexeme, "for") == 0) { 
                fprintf(out, "For Loop\n"); 
            } else if (strcmp(node->token.lexeme, "read") == 0) { 
                fprintf(out, "Read Statement\n"); 
            } else if (strcmp(node->token.lexeme, "write") == 0) { 
                fprintf(out, "Write Statement\n"); 
            } else if (strcmp(node->token.lexeme, "return") == 0) { 
                fprintf(out, "Return Statement\n"); 
            } else if (strcmp(node->token.lexeme, "break") == 0) { 
                fprintf(out, "Break Statement\n"); 
            } else if (strcmp(node->token.lexeme, "continue") == 0) { 
                fprintf(out, "Continue Statement\n"); 
            } else if (strcmp(node->token.lexeme, "int") == 0) { 
                fprintf(out, "Type: int\n"); 
            } else { 
                fprintf(out, "Keyword: %s\n", node->token.lexeme); 
            } 
            break; 
        case TOKEN_DELIMITER: 
            if (node->token.lexeme[0] == '{') { 
                fprintf(out, "Compound Statement\n"); 
            } else { 
                fprintf(out, "Delimiter: %s\n", node->token.lexeme); 
            } 
            break; 
        default: 
            fprintf(out, "Unknown: %s\n", node->token.lexeme); 
    } 

    print_tree(out, node->left, depth + 1); 
    print_tree(out, node->mid, depth + 1); 
    print_tree(out, node->right, depth + 1); 

    if (node->next) { 
        print_tree(out, node->next, depth); 
    } 
} 

void print_tree_json(FILE *out, ASTNode *node, int depth) { 
    if (!node) { 
        fprintf(out, "null"); 
        return; 
    } 

    fprintf(out, "{\n");

    for (int i = 0; i <= depth; i++) fprintf(out, "  ");
    fprintf(out, "\"type\": ");
    // 保存当前节点的行号，用于后续输出
    int node_line = node->token.line_no; 

    switch (node->token.type) { 
        case TOKEN_IDENTIFIER:
            fprintf(out, "\"Identifier\",\n");
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"line\": %d,\n", node_line);
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"name\": \"%s\"", node->token.lexeme);
            break; 
        case TOKEN_NUM:
            fprintf(out, "\"Literal\",\n");
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"line\": %d,\n", node_line);
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"value\": %s,\n", node->token.lexeme);
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"raw\": \"%s\"", node->token.lexeme);
            break; 
        case TOKEN_OPERATOR:
            fprintf(out, "\"BinaryExpression\",\n");
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"line\": %d,\n", node_line);
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"operator\": \"%s\",\n", node->token.lexeme);
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"left\": ");
            print_tree_json(out, node->left, depth + 1);
            fprintf(out, ",\n");
            for (int i = 0; i <= depth; i++) fprintf(out, "  ");
            fprintf(out, "\"right\": ");
            print_tree_json(out, node->right, depth + 1); 
            break; 
        case TOKEN_KEYWORD: 
            if (strcmp(node->token.lexeme, "program") == 0) {
                fprintf(out, "\"Program\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"body\": [\n"); 
                if (node->right) { 
                    for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                    print_tree_json(out, node->right, depth + 2); 
                } 
                fprintf(out, "\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "]"); 
            } else if (strcmp(node->token.lexeme, "main") == 0) {
                fprintf(out, "\"FunctionDeclaration\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"id\": {\"type\": \"Identifier\", \"name\": \"main\"},\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"body\": "); 
                print_tree_json(out, node->right, depth + 1); 
            } else if (strcmp(node->token.lexeme, "var") == 0) {
                fprintf(out, "\"VariableDeclaration\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"kind\": \"var\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"declarations\": [\n"); 
                for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                fprintf(out, "{\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"type\": \"VariableDeclarator\",\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"id\": "); 
                print_tree_json(out, node->left, depth + 3); 
                fprintf(out, ",\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"kind\": \"int\""); 
                if (node->mid) { 
                    fprintf(out, ",\n"); 
                    for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                    fprintf(out, "\"init\": "); 
                    print_tree_json(out, node->mid, depth + 3); 
                } 
                fprintf(out, "\n"); 
                for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                fprintf(out, "}\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "]"); 
            } else if (strcmp(node->token.lexeme, "let") == 0) {
                fprintf(out, "\"VariableDeclaration\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"kind\": \"let\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"declarations\": [\n"); 
                for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                fprintf(out, "{\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"type\": \"VariableDeclarator\",\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"id\": "); 
                print_tree_json(out, node->left, depth + 3); 
                fprintf(out, ",\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"kind\": \"int\""); 
                if (node->mid) { 
                    fprintf(out, ",\n"); 
                    for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                    fprintf(out, "\"init\": "); 
                    print_tree_json(out, node->mid, depth + 3); 
                } 
                fprintf(out, "\n"); 
                for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                fprintf(out, "}\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "]"); 
            } else if (strcmp(node->token.lexeme, "const") == 0) { 
                fprintf(out, "\"VariableDeclaration\",\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "\"kind\": \"const\",\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "\"declarations\": [\n"); 
                for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                fprintf(out, "{\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"type\": \"VariableDeclarator\",\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"id\": "); 
                print_tree_json(out, node->left, depth + 3); 
                fprintf(out, ",\n"); 
                for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                fprintf(out, "\"kind\": \"int\""); 
                if (node->mid) { 
                    fprintf(out, ",\n"); 
                    for (int i = 0; i <= depth + 2; i++) fprintf(out, "  "); 
                    fprintf(out, "\"init\": "); 
                    print_tree_json(out, node->mid, depth + 3); 
                } 
                fprintf(out, "\n"); 
                for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                fprintf(out, "}\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "]"); 
            } else if (strcmp(node->token.lexeme, "if") == 0) {
                fprintf(out, "\"IfStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"test\": "); 
                print_tree_json(out, node->left, depth + 1); 
                fprintf(out, ",\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "\"consequent\": "); 
                print_tree_json(out, node->mid, depth + 1); 
                if (node->right) { 
                    fprintf(out, ",\n"); 
                    for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                    fprintf(out, "\"alternate\": "); 
                    print_tree_json(out, node->right, depth + 1); 
                } 
            } else if (strcmp(node->token.lexeme, "while") == 0) {
                fprintf(out, "\"WhileStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"test\": "); 
                print_tree_json(out, node->left, depth + 1); 
                fprintf(out, ",\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "\"body\": "); 
                print_tree_json(out, node->right, depth + 1); 
            } else if (strcmp(node->token.lexeme, "for") == 0) {
                fprintf(out, "\"ForStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "\"left\": "); 
                print_tree_json(out, node->left, depth + 1); 
                fprintf(out, ",\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "\"right\": "); 
                print_tree_json(out, node->mid, depth + 1); 
                fprintf(out, ",\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "\"body\": "); 
                print_tree_json(out, node->right, depth + 1); 
            } else if (strcmp(node->token.lexeme, "read") == 0) {
                fprintf(out, "\"ReadStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"argument\": ");
                print_tree_json(out, node->left, depth + 1);
            } else if (strcmp(node->token.lexeme, "write") == 0) {
                fprintf(out, "\"WriteStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"argument\": ");
                print_tree_json(out, node->left, depth + 1);
            } else if (strcmp(node->token.lexeme, "return") == 0) {
                fprintf(out, "\"ReturnStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"argument\": ");
                print_tree_json(out, node->left, depth + 1);
            } else if (strcmp(node->token.lexeme, "break") == 0) {
                fprintf(out, "\"BreakStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d", node_line);
            } else if (strcmp(node->token.lexeme, "continue") == 0) {
                fprintf(out, "\"ContinueStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d", node_line);
            } else {
                fprintf(out, "\"Keyword\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"value\": \"%s\"", node->token.lexeme);
            } 
            break; 
        case TOKEN_DELIMITER:
            if (node->token.lexeme[0] == '{') {
                fprintf(out, "\"BlockStatement\",\n");
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"line\": %d,\n", node_line);
                for (int i = 0; i <= depth; i++) fprintf(out, "  ");
                fprintf(out, "\"body\": [\n"); 
                ASTNode *child = node->left; 
                int first = 1; 
                while (child) { 
                    if (!first) fprintf(out, ",\n"); 
                    first = 0; 
                    for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                    print_tree_json(out, child, depth + 2); 
                    child = child->next; 
                } 
                child = node->right; 
                while (child) { 
                    if (!first) fprintf(out, ",\n"); 
                    first = 0; 
                    for (int i = 0; i <= depth + 1; i++) fprintf(out, "  "); 
                    print_tree_json(out, child, depth + 2); 
                    child = child->next; 
                } 
                fprintf(out, "\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "]"); 
            } else { 
                fprintf(out, "\"Delimiter\",\n"); 
                for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
                fprintf(out, "\"value\": \"%s\"", node->token.lexeme); 
            } 
            break; 
        default: 
            fprintf(out, "\"Unknown\",\n"); 
            for (int i = 0; i <= depth; i++) fprintf(out, "  "); 
            fprintf(out, "\"value\": \"%s\"", node->token.lexeme); 
    } 

    fprintf(out, "\n"); 
    for (int i = 0; i < depth; i++) fprintf(out, "  "); 
    fprintf(out, "}"); 
} 

void free_tree(ASTNode *node) { 
    if (!node) return; 
    free_tree(node->left); 
    free_tree(node->mid); 
    free_tree(node->right); 
    free_tree(node->next); 
    free(node); 
} 

int main(int argc, char *argv[]) { 
    if (argc != 3) { 
        fprintf(stderr, "Usage: %s <token_file> <ast_output_file>\n", argv[0]); 
        return 1; 
    } 

    token_file = fopen(argv[1], "r"); 
    if (!token_file) { 
        fprintf(stderr, "Error: cannot open token file '%s'\n", argv[1]); 
        return 1; 
    } 

    printf("===== Reading Tokens from '%s' =====\n", argv[1]); 
    next_token(); 
    while (current_token.type != TOKEN_EOF) { 
        const char *type_str; 
        switch (current_token.type) { 
            case TOKEN_KEYWORD: type_str = "KEYWORD"; break; 
            case TOKEN_IDENTIFIER: type_str = "IDENT"; break; 
            case TOKEN_NUM: type_str = "NUM"; break; 
            case TOKEN_OPERATOR: type_str = "OP"; break; 
            case TOKEN_DELIMITER: type_str = "DELIM"; break; 
            case TOKEN_ERROR: type_str = "ERROR"; break; 
            case TOKEN_EOF: type_str = "EOF"; break; 
            default: type_str = "UNKNOWN"; 
        } 
        printf("%-10s %-15s %d\n", type_str, current_token.lexeme, current_token.line_no); 
        next_token(); 
    } 

    printf("\n===== Syntax Analysis =====\n"); 
    // 重新打开文件读取 
    fclose(token_file); 
    token_file = fopen(argv[1], "r"); 
    has_more_tokens = true; 
    next_token(); 

    ASTNode *tree = program(); 

    printf("\nAbstract Syntax Tree:\n"); 
    print_tree(stdout, tree, 0); 

    FILE *ast_file = fopen(argv[2], "w"); 
    if (ast_file) { 
        fprintf(ast_file, "{\n"); 
        fprintf(ast_file, "  \"type\": \"Program\",\n"); 
        fprintf(ast_file, "  \"body\": [\n"); 
        print_tree_json(ast_file, tree->right, 2); 
        fprintf(ast_file, "\n  ]\n"); 
        fprintf(ast_file, "}\n"); 
        fclose(ast_file); 
        printf("\nJSON syntax tree saved to '%s'\n", argv[2]); 
    } else { 
        fprintf(stderr, "Warning: could not save syntax tree to file\n"); 
    } 

    free_tree(tree); 
    fclose(token_file); 

    // 输出错误摘要
    if (has_errors) { 
        printf("\n===== Error Summary =====\n"); 
        printf("Total errors found: %d\n", error_count > MAX_ERRORS ? MAX_ERRORS : error_count); 
        if (error_count > MAX_ERRORS) { 
            printf("(Note: Only first %d errors were reported)\n", MAX_ERRORS); 
        } 
        printf("Analysis completed with errors.\n"); 
        return 1; 
    } else { 
        printf("\nAnalysis completed successfully!\n"); 
        return 0; 
    } 
}
