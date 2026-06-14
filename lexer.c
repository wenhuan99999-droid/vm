#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <ctype.h> 
#include <stdbool.h> 

#define MAX_IDENT_LEN 32 
#define MAX_TOKEN_LEN 64 

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

static const char *keywords[] = { 
    "as", "break", "case", "const", "continue", "do", "else", "for", 
    "from", "func", "if", "in", "int", "let", "main", "match", 
    "read", "return", "var", "where", "while", "write" 
}; 
#define KEYWORD_COUNT (sizeof(keywords)/sizeof(keywords[0])) 

static FILE *in = NULL; 
static FILE *out = NULL; 
static int line = 1; 
static int curr_char = ' '; 
static bool have_char = false; 

static void get_char(void); 
static bool skip_comments(void); 
static Token next_token(void); 
static int is_keyword(const char *s); 
static void read_ident(char *buf, int *len); 
static bool read_number(char *buf, int *len, int *val); 

static void get_char(void) { 
    int c = fgetc(in); 
    if (c == EOF) { 
        have_char = false; 
        curr_char = '\0'; 
    } else { 
        have_char = true; 
        curr_char = c; 
        if (curr_char == '\n') line++; 
    } 
} 

static bool skip_comments(void) { 
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
                    fprintf(stderr, "Error: unclosed comment at line %d\n", line); 
                    return false; 
                } 
                continue; 
            } else { 
                ungetc('/', in); 
                get_char(); 
                break; 
            } 
        } 
        break; 
    } 
    return true; 
} 

static int is_keyword(const char *s) { 
    int low = 0, high = KEYWORD_COUNT - 1; 
    while (low <= high) { 
        int mid = (low + high) / 2; 
        int cmp = strcmp(s, keywords[mid]); 
        if (cmp == 0) return mid + 1; 
        else if (cmp < 0) high = mid - 1; 
        else low = mid + 1; 
    } 
    return 0; 
} 

static void read_ident(char *buf, int *len) { 
    *len = 0; 
    while (have_char && (isalnum(curr_char) || curr_char == '_')) { 
        if (*len < MAX_IDENT_LEN - 1) buf[(*len)++] = curr_char; 
        get_char(); 
    } 
    buf[*len] = '\0'; 
} 

static bool read_number(char *buf, int *len, int *val) { 
    *len = 0; 
    *val = 0; 
    while (have_char && isdigit(curr_char)) { 
        if (*len < MAX_TOKEN_LEN - 1) buf[(*len)++] = curr_char; 
        *val = *val * 10 + (curr_char - '0'); 
        get_char(); 
    } 
    if (have_char && curr_char == '.') { 
        buf[(*len)++] = '.'; 
        get_char(); 
        while (have_char && isdigit(curr_char)) { 
            if (*len < MAX_TOKEN_LEN - 1) buf[(*len)++] = curr_char; 
            get_char(); 
        } 
        buf[*len] = '\0'; 
        return false; 
    } 
    buf[*len] = '\0'; 
    return true; 
} 

static Token next_token(void) { 
    Token tok = {.type = TOKEN_EOF, .lexeme = "", .line_no = line, .int_value = 0}; 

    if (!skip_comments()) { 
        tok.type = TOKEN_ERROR; 
        strcpy(tok.lexeme, "Unclosed comment"); 
        return tok; 
    } 

    if (!have_char) { 
        tok.type = TOKEN_EOF; 
        strcpy(tok.lexeme, "EOF"); 
        return tok; 
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
        return tok; 
    } 

    if (isdigit(curr_char)) { 
        char buf[MAX_TOKEN_LEN]; 
        int len; 
        if (!read_number(buf, &len, &tok.int_value)) { 
            tok.type = TOKEN_ERROR; 
            strcpy(tok.lexeme, buf); 
            return tok; 
        } 
        tok.type = TOKEN_NUM; 
        strcpy(tok.lexeme, buf); 
        return tok; 
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
            return tok; 
        } 
    } 
    if (c == '.' && have_char && curr_char == '.') { 
        tok.lexeme[1] = curr_char; 
        tok.lexeme[2] = '\0'; 
        get_char(); 
        tok.type = TOKEN_DELIMITER; 
        return tok; 
    } 

    switch (c) { 
        case '+': 
        case '-': 
        case '*': 
        case '/': 
        case '=': 
        case '!': 
        case '<': 
        case '>': 
            tok.type = TOKEN_OPERATOR; 
            return tok; 
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
            return tok; 
        default: 
            tok.type = TOKEN_ERROR; 
            sprintf(tok.lexeme, "Unknown character '%c'", c); 
            return tok; 
    } 
} 

static const char* token_type_to_string(TokenType type) { 
    switch (type) { 
        case TOKEN_KEYWORD: return "KEYWORD"; 
        case TOKEN_IDENTIFIER: return "IDENT"; 
        case TOKEN_NUM: return "NUM"; 
        case TOKEN_OPERATOR: return "OP"; 
        case TOKEN_DELIMITER: return "DELIM"; 
        case TOKEN_ERROR: return "ERROR"; 
        case TOKEN_EOF: return "EOF"; 
        default: return "UNKNOWN"; 
    } 
} 

int main(int argc, char *argv[]) { 
    if (argc != 3) { 
        fprintf(stderr, "Usage: %s <input_file> <output_file>\n", argv[0]); 
        return 1; 
    } 

    in = fopen(argv[1], "r"); 
    if (!in) { 
        fprintf(stderr, "Error: cannot open input file '%s'\n", argv[1]); 
        return 1; 
    } 

    out = fopen(argv[2], "w"); 
    if (!out) { 
        fprintf(stderr, "Error: cannot open output file '%s'\n", argv[2]); 
        fclose(in); 
        return 1; 
    } 

    get_char(); 

    fprintf(out, "===== Lexical Analysis Output =====\n"); 
    fprintf(out, "%-10s %-20s %-6s %s\n", "Type", "Lexeme", "Line", "Value"); 
    fprintf(out, "--------------------------------------------\n"); 

    int token_count = 0; 
    Token tok; 
    do { 
        tok = next_token(); 
        fprintf(out, "%-10s %-20s %-6d", token_type_to_string(tok.type), tok.lexeme, tok.line_no); 
        if (tok.type == TOKEN_NUM) { 
            fprintf(out, "%d", tok.int_value); 
        } 
        fprintf(out, "\n"); 
        token_count++; 
    } while (tok.type != TOKEN_EOF && tok.type != TOKEN_ERROR); 

    fprintf(out, "\nTotal tokens: %d\n", token_count); 

    printf("Lexical analysis completed. Tokens saved to '%s'\n", argv[2]); 

    fclose(in); 
    fclose(out); 
    return (tok.type == TOKEN_ERROR) ? 1 : 0; 
}
