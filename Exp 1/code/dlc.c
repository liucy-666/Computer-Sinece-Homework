#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_LINE 4096

typedef struct {
    char name[32];
    char legal_ops[32];
    int max_ops;
} FuncRule;

FuncRule rules[] = {
    {"bitAnd", "~|", 8},
    {"bitXor", "~&", 14},
    {"evenBits", "!~&^|+<<>>", 8},
    {"getByte", "!~&^|+<<>>", 6},
    {"bitMask", "!~&^|+<<>>", 16},
    {"reverseBytes", "!~&^|+<<>>", 25},
    {"leastBitPos", "!~&^|+<<>>", 6},
    {"logicalNeg", "~&^|+<<>>", 12},
    {"minusOne", "!~&^|+<<>>", 2},
    {"tmax", "!~&^|+<<>>", 4},
    {"negate", "!~&^|+<<>>", 5},
    {"isPositive", "!~&^|+<<>>", 8},
    {"isLess", "!~&^|+<<>>", 24},
    {"sm2tc", "!~&^|+<<>>", 15},
    {"", "", 0}
};

const char *forbidden_keywords[] = {
    "if", "while", "for", "switch", "printf", "scanf", "return 2;"
};
#define FORBID_CNT sizeof(forbidden_keywords)/sizeof(char*)

FuncRule *findRule(const char *name) {
    for (int i = 0; rules[i].name[0]; i++)
        if (strcmp(name, rules[i].name) == 0) return &rules[i];
    return NULL;
}

int isLegalOp(char c, const char *legal) {
    for (int i = 0; legal[i]; i++)
        if (c == legal[i]) return 1;
    return 0;
}

int isWordChar(char c) {
    return isalnum(c) || c == '_';
}

const char* findForbiddenWord(const char *line) {
    for (int i = 0; i < FORBID_CNT; i++) {
        const char *kw = forbidden_keywords[i];
        const char *pos = line;
        while ((pos = strstr(pos, kw)) != NULL) {
            int is_word = 1;
            if (pos > line && isWordChar(*(pos-1))) is_word = 0;
            if (is_word && pos[strlen(kw)] && isWordChar(pos[strlen(kw)])) is_word = 0;
            if (is_word || strchr(kw, ' ')) return kw;
            pos++;
        }
    }
    return NULL;
}

void removeComment(char *line) {
    char *p = strstr(line, "//");
    if (p) *p = 0;
    p = strstr(line, "/*");
    if (p) *p = 0;
}

int countOpsAndCheck(char *line, const char *legal, int *illegal) {
    int cnt = 0;
    for (int i = 0; line[i]; i++) {
        char c = line[i];
        if (c == '~' || c == '!' || c == '&' || c == '^' || c == '|' ||
            c == '+' || c == '<' || c == '>' || c == '-') {
            if (c == '<' && line[i+1] == '<') { cnt++; i++; continue; }
            if (c == '>' && line[i+1] == '>') { cnt++; i++; continue; }
            cnt++;
            if (!isLegalOp(c, legal)) *illegal = 1;
        }
    }
    return cnt;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    FILE *f = fopen("bits.c", "r");
    if (!f) { perror("bits.c"); return 1; }

    char line[MAX_LINE];
    printf("=============================================\n");
    printf("      Final DLC Checker (No False Positives)\n");
    printf("=============================================\n\n");

    int in_func = 0;
    FuncRule *rule = NULL;
    int op_cnt = 0;
    int illegal_op = 0;
    const char *forbid_reason = NULL;

    while (fgets(line, MAX_LINE, f)) {
        if (!in_func) {
            char name[32] = {0};
            if (sscanf(line, "int %[^(]", name) == 1) {
                rule = findRule(name);
                if (rule) {
                    in_func = 1;
                    op_cnt = 0;
                    illegal_op = 0;
                    forbid_reason = NULL;
                }
            }
        } else {
            char line_copy[MAX_LINE];
            strcpy(line_copy, line);
            removeComment(line);
            
            if (!forbid_reason) {
                forbid_reason = findForbiddenWord(line);
            }
            
            op_cnt += countOpsAndCheck(line, rule->legal_ops, &illegal_op);

            if (strchr(line, '}')) {
                if (!illegal_op && !forbid_reason && op_cnt <= rule->max_ops)
                    printf("[PASS] %-15s | ops: %d/%d\n", rule->name, op_cnt, rule->max_ops);
                else {
                    printf("[FAIL] %-15s | reason: ", rule->name);
                    if (illegal_op) printf("illegal operator ");
                    if (forbid_reason) printf("found forbidden '%s' ", forbid_reason);
                    if (op_cnt > rule->max_ops) printf("too many ops (%d/%d) ", op_cnt, rule->max_ops);
                    printf("\n");
                }
                in_func = 0;
                rule = NULL;
            }
        }
    }

    printf("\nCheck complete!\n");
    fclose(f);
    return 0;
}