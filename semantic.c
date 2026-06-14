#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_IDENT_LEN 32
#define MAX_CODE_LINES 500
#define MAX_SYMBOLS 100

// ============ 符号表定义 ============
typedef enum {
    SYM_VAR,
    SYM_FUNC
} SymbolKind;

typedef struct {
    char name[MAX_IDENT_LEN];
    SymbolKind kind;
    char type[MAX_IDENT_LEN];
    int scope;
    int offset;
} Symbol;

static Symbol symbol_table[MAX_SYMBOLS];
static int symbol_count = 0;
static int current_scope = 0;
static int current_offset = 0;

// ============ 中间代码定义 ============
typedef struct {
    char op[12];
    char arg[MAX_IDENT_LEN];
    int line_no;
} CodeLine;

static CodeLine code[MAX_CODE_LINES];
static int code_count = 0;
static int label_count = 0;

// ============ 错误处理 ============
static int error_count = 0;
static bool has_errors = false;
#define MAX_ERRORS 20

// ============ JSON解析器定义 ============
typedef enum {
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_STRING,
    JSON_NUMBER,
    JSON_NULL,
    JSON_OTHER
} JsonType;

typedef struct JsonNode {
    JsonType type;
    char *key;
    char *value;
    int line;
    struct JsonNode *next;
    struct JsonNode *children;
} JsonNode;

// ============ 函数声明 ============
void error(const char *message, int line);
int lookup_symbol(const char *name, int scope);
int insert_symbol(const char *name, SymbolKind kind, const char *type, int scope);
void enter_scope(void);
void exit_scope(void);
void generate_code(const char *op, const char *arg, int line);
char* new_label(const char *prefix);
void print_code(FILE *out);
void print_symbol_table(FILE *out);
JsonNode* json_parse(const char **ptr);
JsonNode* json_get_child(JsonNode *obj, const char *key);
void process_node(JsonNode *node);
void json_free(JsonNode *node);

// ============ 错误处理函数 ============
void error(const char *message, int line) {
    if (error_count < MAX_ERRORS) {
        fprintf(stderr, "Semantic Error %d: %s at line %d\n", error_count + 1, message, line);
        error_count++;
        has_errors = true;
    } else if (error_count == MAX_ERRORS) {
        fprintf(stderr, "Too many semantic errors. Stopping error reporting.\n");
        error_count++;
    }
}

// ============ 符号表操作 ============
int lookup_symbol(const char *name, int scope) {
    for (int i = symbol_count - 1; i >= 0; i--) {
        if (strcmp(symbol_table[i].name, name) == 0) {
            if (symbol_table[i].scope <= scope) {
                return i;
            }
        }
    }
    return -1;
}

int insert_symbol(const char *name, SymbolKind kind, const char *type, int scope) {
    for (int i = symbol_count - 1; i >= 0; i--) {
        if (strcmp(symbol_table[i].name, name) == 0 && symbol_table[i].scope == scope) {
            return -1;
        }
    }
    
    if (symbol_count >= MAX_SYMBOLS) {
        error("Symbol table overflow", 0);
        return -1;
    }
    
    strcpy(symbol_table[symbol_count].name, name);
    symbol_table[symbol_count].kind = kind;
    strcpy(symbol_table[symbol_count].type, type);
    symbol_table[symbol_count].scope = scope;
    symbol_table[symbol_count].offset = current_offset++;
    
    return symbol_count++;
}

void enter_scope(void) {
    current_scope++;
}

void exit_scope(void) {
    // 不删除符号，只改变当前作用域级别，以便保留符号表信息
    current_scope--;
}

// ============ 代码生成辅助函数 ============
void generate_code(const char *op, const char *arg, int line) {
    if (code_count >= MAX_CODE_LINES) {
        error("Code buffer overflow", line);
        return;
    }
    strcpy(code[code_count].op, op);
    strcpy(code[code_count].arg, arg);
    code[code_count].line_no = line;
    code_count++;
}

char* new_label(const char *prefix) {
    static char label[MAX_IDENT_LEN];
    sprintf(label, "%s%d", prefix, label_count++);
    return label;
}

// ============ 输出函数 ============
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
    fprintf(out, "%-15s %-8s %-8s %-6s %-6s\n", "Name", "Kind", "Type", "Scope", "Offset");
    fprintf(out, "----------------------------------------\n");
    
    for (int i = 0; i < symbol_count; i++) {
        const char *kind_str = (symbol_table[i].kind == SYM_VAR) ? "VAR" : "FUNC";
        fprintf(out, "%-15s %-8s %-8s %-6d %-6d\n",
                symbol_table[i].name,
                kind_str,
                symbol_table[i].type,
                symbol_table[i].scope,
                symbol_table[i].offset);
    }
    
    fprintf(out, "\nTotal symbols: %d\n", symbol_count);
}

// ============ JSON解析器 ============
static char* json_parse_string(const char **ptr) {
    if (**ptr != '"') return NULL;
    (*ptr)++;
    char *str = malloc(256);
    int len = 0;
    while (**ptr != '"' && **ptr != '\0') {
        str[len++] = **ptr;
        (*ptr)++;
    }
    str[len] = '\0';
    (*ptr)++;
    return str;
}

static void json_skip_ws(const char **ptr) {
    while (**ptr == ' ' || **ptr == '\t' || **ptr == '\n' || **ptr == '\r') (*ptr)++;
}

static JsonNode* json_parse_value(const char **ptr);
static JsonNode* json_parse_array(const char **ptr);

JsonNode* json_parse(const char **ptr) {
    if (**ptr != '{') return NULL;
    (*ptr)++;
    json_skip_ws(ptr);
    
    JsonNode *head = NULL, *tail = NULL;
    
    if (**ptr != '}') {
        while (1) {
            json_skip_ws(ptr);
            char *key = json_parse_string(ptr);
            if (!key) break;
            
            json_skip_ws(ptr);
            if (**ptr != ':') { free(key); break; }
            (*ptr)++;
            
            json_skip_ws(ptr);
            JsonNode *value = json_parse_value(ptr);
            if (!value) { free(key); break; }
            
            value->key = key;
            
            if (!head) { head = value; tail = value; }
            else { tail->next = value; tail = value; }
            
            json_skip_ws(ptr);
            if (**ptr == ',') (*ptr)++;
            else break;
        }
    }
    
    (*ptr)++;
    
    JsonNode *obj = malloc(sizeof(JsonNode));
    obj->type = JSON_OBJECT;
    obj->key = NULL;
    obj->value = NULL;
    obj->line = 0;
    obj->children = head;
    obj->next = NULL;
    
    // 查找line字段并设置
    JsonNode *child = head;
    while (child) {
        if (child->key && strcmp(child->key, "line") == 0) {
            obj->line = atoi(child->value);
            break;
        }
        child = child->next;
    }
    
    return obj;
}

static JsonNode* json_parse_array(const char **ptr) {
    if (**ptr != '[') return NULL;
    (*ptr)++;
    json_skip_ws(ptr);
    
    JsonNode *head = NULL, *tail = NULL;
    
    if (**ptr != ']') {
        while (1) {
            json_skip_ws(ptr);
            JsonNode *value = json_parse_value(ptr);
            if (!value) break;
            
            value->key = NULL;
            
            if (!head) { head = value; tail = value; }
            else { tail->next = value; tail = value; }
            
            json_skip_ws(ptr);
            if (**ptr == ',') (*ptr)++;
            else break;
        }
    }
    
    if (**ptr == ']') (*ptr)++;
    
    JsonNode *arr = malloc(sizeof(JsonNode));
    arr->type = JSON_ARRAY;
    arr->key = NULL;
    arr->value = NULL;
    arr->children = head;
    arr->next = NULL;
    
    return arr;
}

static JsonNode* json_parse_value(const char **ptr) {
    json_skip_ws(ptr);
    if (**ptr == '{') return json_parse(ptr);
    if (**ptr == '[') return json_parse_array(ptr);
    if (**ptr == '"') {
        JsonNode *node = malloc(sizeof(JsonNode));
        node->type = JSON_STRING;
        node->key = NULL;
        node->value = json_parse_string(ptr);
        node->next = NULL;
        node->children = NULL;
        return node;
    }
    if (isdigit(**ptr) || **ptr == '-') {
        char num_str[32];
        int len = 0;
        while (isdigit(**ptr) || **ptr == '.' || **ptr == '-') {
            num_str[len++] = **ptr;
            (*ptr)++;
        }
        num_str[len] = '\0';
        JsonNode *node = malloc(sizeof(JsonNode));
        node->type = JSON_NUMBER;
        node->key = NULL;
        node->value = malloc(strlen(num_str) + 1);
        strcpy(node->value, num_str);
        node->next = NULL;
        node->children = NULL;
        return node;
    }
    if (strncmp(*ptr, "true", 4) == 0) { (*ptr) += 4; }
    else if (strncmp(*ptr, "false", 5) == 0) { (*ptr) += 5; }
    else if (strncmp(*ptr, "null", 4) == 0) { 
        (*ptr) += 4; 
        JsonNode *node = malloc(sizeof(JsonNode));
        node->type = JSON_NULL;
        node->key = NULL;
        node->value = NULL;
        node->next = NULL;
        node->children = NULL;
        return node;
    }
    
    JsonNode *node = malloc(sizeof(JsonNode));
    node->type = JSON_OTHER;
    node->key = NULL;
    node->value = NULL;
    node->next = NULL;
    node->children = NULL;
    return node;
}

JsonNode* json_get_child(JsonNode *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !obj->children) return NULL;
    JsonNode *child = obj->children;
    while (child) {
        if (child->key && strcmp(child->key, key) == 0) return child;
        child = child->next;
    }
    return NULL;
}

void json_free(JsonNode *node) {
    if (!node) return;
    if (node->key) free(node->key);
    if (node->value) free(node->value);
    json_free(node->children);
    json_free(node->next);
    free(node);
}

// ============ 语义分析函数 ============
void process_node(JsonNode *node) {
    if (!node) return;
    
    if (node->type == JSON_ARRAY) {
        JsonNode *elem = node->children;
        while (elem) {
            process_node(elem);
            elem = elem->next;
        }
        return;
    }
    
    if (node->type != JSON_OBJECT) return;
    
    JsonNode *type_node = json_get_child(node, "type");
    if (!type_node || type_node->type != JSON_STRING) {
        JsonNode *child = node->children;
        while (child) {
            process_node(child);
            child = child->next;
        }
        return;
    }
    
    const char *type_str = type_node->value;
    
    if (strcmp(type_str, "Program") == 0) {
        JsonNode *body_node = json_get_child(node, "body");
        if (body_node) process_node(body_node);
    } else if (strcmp(type_str, "FunctionDeclaration") == 0) {
        JsonNode *id_node = json_get_child(node, "id");
        if (id_node) {
            JsonNode *name_node = json_get_child(id_node, "name");
            if (name_node && name_node->type == JSON_STRING && name_node->value) {
                insert_symbol(name_node->value, SYM_FUNC, "void", current_scope);
            }
        }
        
        enter_scope();
        
        JsonNode *body_node = json_get_child(node, "body");
        if (body_node) process_node(body_node);
        
        exit_scope();
        
        generate_code("STOP", "", 0);
    } else if (strcmp(type_str, "BlockStatement") == 0) {
        enter_scope();
        
        JsonNode *body_node = json_get_child(node, "body");
        if (body_node) process_node(body_node);
        
        exit_scope();
    } else if (strcmp(type_str, "VariableDeclaration") == 0) {
        JsonNode *decls_node = json_get_child(node, "declarations");
        if (decls_node && decls_node->type == JSON_ARRAY) {
            JsonNode *decl_node = decls_node->children;
            if (decl_node && decl_node->type == JSON_OBJECT) {
                JsonNode *id_node = json_get_child(decl_node, "id");
                if (id_node && id_node->type == JSON_OBJECT) {
                    JsonNode *name_node = json_get_child(id_node, "name");
                    if (name_node && name_node->type == JSON_STRING && name_node->value) {
                        const char *name = name_node->value;
                        
                        if (lookup_symbol(name, current_scope) != -1) {
                        error("Variable already declared", node->line);
                        return;
                    }
                        
                        insert_symbol(name, SYM_VAR, "int", current_scope);
                        
                        JsonNode *init_node = json_get_child(decl_node, "init");
                        if (init_node && init_node->type != JSON_OTHER) {
                            process_node(init_node);
                            int idx = lookup_symbol(name, current_scope);
                            if (idx != -1) {
                                char arg[MAX_IDENT_LEN];
                                sprintf(arg, "%d", symbol_table[idx].offset);
                                generate_code("STO", arg, 0);
                            }
                        }
                    }
                }
            }
        }
    } else if (strcmp(type_str, "IfStatement") == 0) {
        JsonNode *test_node = json_get_child(node, "test");
        if (test_node) process_node(test_node);
        
        char *else_label = new_label("ELSE");
        generate_code("BRF", else_label, 0);
        
        JsonNode *consequent_node = json_get_child(node, "consequent");
        if (consequent_node) process_node(consequent_node);
        
        JsonNode *alternate_node = json_get_child(node, "alternate");
        if (alternate_node && alternate_node->type != JSON_OTHER && alternate_node->type != JSON_NULL) {
            char *end_label = new_label("ENDIF");
            generate_code("BR", end_label, 0);
            generate_code(else_label, ":", 0);
            process_node(alternate_node);
            generate_code(end_label, ":", 0);
        } else {
            generate_code(else_label, ":", 0);
        }
    } else if (strcmp(type_str, "WhileStatement") == 0) {
        char *loop_label = new_label("WHILE");
        char *end_label = new_label("ENDWHILE");
        
        generate_code(loop_label, ":", 0);
        
        JsonNode *test_node = json_get_child(node, "test");
        if (test_node) process_node(test_node);
        
        generate_code("BRF", end_label, 0);
        
        enter_scope();
        JsonNode *body_node = json_get_child(node, "body");
        if (body_node) process_node(body_node);
        exit_scope();
        
        generate_code("BR", loop_label, 0);
        generate_code(end_label, ":", 0);
    } else if (strcmp(type_str, "ForStatement") == 0) {
        enter_scope();
        
        JsonNode *left_node = json_get_child(node, "left");
        if (left_node && left_node->type == JSON_OBJECT) {
            JsonNode *id_node = json_get_child(left_node, "name");
            if (id_node && id_node->type == JSON_STRING && id_node->value) {
                insert_symbol(id_node->value, SYM_VAR, "int", current_scope);
            }
        }
        
        JsonNode *right_node = json_get_child(node, "right");
        if (right_node) process_node(right_node);
        
        JsonNode *body_node = json_get_child(node, "body");
        if (body_node) process_node(body_node);
        
        exit_scope();
    } else if (strcmp(type_str, "ReadStatement") == 0) {
        JsonNode *arg_node = json_get_child(node, "argument");
        if (arg_node && arg_node->type == JSON_OBJECT) {
            JsonNode *name_node = json_get_child(arg_node, "name");
            if (name_node && name_node->type == JSON_STRING && name_node->value) {
                int idx = lookup_symbol(name_node->value, current_scope);
                if (idx == -1) {
                    error("Undefined variable", arg_node->line);
                } else {
                    generate_code("IN", "", 0);
                    char arg[MAX_IDENT_LEN];
                    sprintf(arg, "%d", symbol_table[idx].offset);
                    generate_code("STO", arg, 0);
                }
            }
        }
    } else if (strcmp(type_str, "WriteStatement") == 0) {
        JsonNode *arg_node = json_get_child(node, "argument");
        if (arg_node) {
            process_node(arg_node);
            generate_code("OUT", "", 0);
        }
    } else if (strcmp(type_str, "ReturnStatement") == 0) {
        JsonNode *arg_node = json_get_child(node, "argument");
        if (arg_node) process_node(arg_node);
        generate_code("RETURN", "", 0);
    } else if (strcmp(type_str, "BreakStatement") == 0) {
        generate_code("BR", "BREAK", 0);
    } else if (strcmp(type_str, "ContinueStatement") == 0) {
        generate_code("BR", "CONTINUE", 0);
    } else if (strcmp(type_str, "ExpressionStatement") == 0) {
        JsonNode *expr_node = json_get_child(node, "expression");
        if (expr_node) process_node(expr_node);
    } else if (strcmp(type_str, "BinaryExpression") == 0) {
        JsonNode *op_node = json_get_child(node, "operator");
        if (op_node && op_node->type == JSON_STRING && op_node->value) {
            const char *op = op_node->value;
            
            if (strcmp(op, "=") == 0) {
                JsonNode *left_node = json_get_child(node, "left");
                if (left_node && left_node->type == JSON_OBJECT) {
                    JsonNode *name_node = json_get_child(left_node, "name");
                    if (name_node && name_node->type == JSON_STRING && name_node->value) {
                        int idx = lookup_symbol(name_node->value, current_scope);
                        if (idx == -1) {
                            error("Undefined variable", left_node->line);
                            return;
                        }
                        
                        JsonNode *right_node = json_get_child(node, "right");
                        if (right_node) process_node(right_node);
                        
                        char arg[MAX_IDENT_LEN];
                        sprintf(arg, "%d", symbol_table[idx].offset);
                        generate_code("STO", arg, 0);
                    }
                }
            } else {
                JsonNode *left_node = json_get_child(node, "left");
                if (left_node) process_node(left_node);
                
                JsonNode *right_node = json_get_child(node, "right");
                if (right_node) process_node(right_node);
                
                if (strcmp(op, "+") == 0) generate_code("ADD", "", 0);
                else if (strcmp(op, "-") == 0) generate_code("SUB", "", 0);
                else if (strcmp(op, "*") == 0) generate_code("MULT", "", 0);
                else if (strcmp(op, "/") == 0) generate_code("DIV", "", 0);
                else if (strcmp(op, "==") == 0) generate_code("EQ", "", 0);
                else if (strcmp(op, "!=") == 0) generate_code("NOTEQ", "", 0);
                else if (strcmp(op, ">") == 0) generate_code("GT", "", 0);
                else if (strcmp(op, "<") == 0) generate_code("LES", "", 0);
                else if (strcmp(op, ">=") == 0) generate_code("GE", "", 0);
                else if (strcmp(op, "<=") == 0) generate_code("LE", "", 0);
            }
        }
    } else if (strcmp(type_str, "Identifier") == 0) {
        JsonNode *name_node = json_get_child(node, "name");
        if (name_node && name_node->type == JSON_STRING && name_node->value) {
            int idx = lookup_symbol(name_node->value, current_scope);
            if (idx == -1) {
                error("Undefined variable", node->line);
            } else {
                char arg[MAX_IDENT_LEN];
                sprintf(arg, "%d", symbol_table[idx].offset);
                generate_code("LOAD", arg, 0);
            }
        }
    } else if (strcmp(type_str, "Literal") == 0) {
        JsonNode *value_node = json_get_child(node, "value");
        if (value_node && value_node->type == JSON_NUMBER && value_node->value) {
            generate_code("LOADI", value_node->value, 0);
        }
    }
}

// ============ 主函数 ============
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ast_json_file> <code_output_file> <symbol_output_file>\n", argv[0]);
        return 1;
    }
    
    FILE *json_file = fopen(argv[1], "r");
    if (!json_file) {
        fprintf(stderr, "Error: cannot open AST file '%s'\n", argv[1]);
        return 1;
    }
    
    fseek(json_file, 0, SEEK_END);
    long size = ftell(json_file);
    fseek(json_file, 0, SEEK_SET);
    
    char *json_content = malloc(size + 1);
    fread(json_content, 1, size, json_file);
    json_content[size] = '\0';
    fclose(json_file);
    
    const char *ptr = json_content;
    JsonNode *json = json_parse(&ptr);
    free(json_content);
    
    if (!json) {
        fprintf(stderr, "Error: failed to parse AST JSON\n");
        return 1;
    }
    
    enter_scope();
    process_node(json);
    exit_scope();
    
    json_free(json);
    
    FILE *symbol_file = fopen(argv[3], "w");
    if (symbol_file) {
        print_symbol_table(symbol_file);
        fclose(symbol_file);
    }
    
    FILE *code_file = fopen(argv[2], "w");
    if (code_file) {
        print_code(code_file);
        fclose(code_file);
    }
    
    if (has_errors) {
        printf("\n===== Error Summary =====\n");
        printf("Total semantic errors found: %d\n", error_count > MAX_ERRORS ? MAX_ERRORS : error_count);
        return 1;
    } else {
        printf("Semantic analysis completed successfully!\n");
        printf("Intermediate code saved to '%s'\n", argv[2]);
        printf("Symbol table saved to '%s'\n", argv[3]);
        return 0;
    }
}