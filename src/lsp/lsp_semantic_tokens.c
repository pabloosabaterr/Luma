/**
 * @file lsp_semantic_tokens.c
 * @brief LSP textDocument/semanticTokens/full implementation for Luma.
 *
 * We classify every token the lexer produced and encode them in the LSP
 * delta-encoded wire format (5 integers per token).
 *
 * Token type legend (indices MUST match the "tokenTypes" array sent in the
 * capabilities response — see lsp_message.c):
 *
 *   Index  Name
 *   -----  ----
 *     0    namespace
 *     1    type
 *     2    typeParameter
 *     3    function
 *     4    method
 *     5    property
 *     6    variable
 *     7    parameter
 *     8    keyword
 *     9    modifier
 *    10    comment
 *    11    string
 *    12    number
 *    13    operator
 *    14    struct
 *    15    enum
 *    16    enumMember
 *
 * Token modifier bitmask (indices MUST match "tokenModifiers"):
 *   Bit 0  declaration
 *   Bit 1  definition
 *   Bit 2  readonly
 *   Bit 3  static
 *   Bit 4  defaultLibrary
 */

#include "lsp.h"
#include <string.h>

enum SemanticTokenType {
    ST_NAMESPACE      =  0,
    ST_TYPE           =  1,
    ST_TYPE_PARAMETER =  2,
    ST_FUNCTION       =  3,
    ST_METHOD         =  4,
    ST_PROPERTY       =  5,
    ST_VARIABLE       =  6,
    ST_PARAMETER      =  7,
    ST_KEYWORD        =  8,
    ST_MODIFIER       =  9,
    ST_COMMENT        = 10,
    ST_STRING         = 11,
    ST_NUMBER         = 12,
    ST_OPERATOR       = 13,
    ST_STRUCT         = 14,
    ST_ENUM           = 15,
    ST_ENUM_MEMBER    = 16,
    ST_COUNT          = 17   /* sentinel — keep last */
};

enum SemanticTokenMod {
    SM_DECLARATION    = (1 << 0),
    SM_DEFINITION     = (1 << 1),
    SM_READONLY       = (1 << 2),
    SM_STATIC         = (1 << 3),
    SM_DEFAULT_LIB    = (1 << 4),
};

typedef struct { int type; int mods; } TokenClass;

static TokenClass classify_token(LSPDocument *doc, size_t tok_idx) {
    Token *tok = &doc->tokens[tok_idx];
    TokenClass none = { -1, 0 };

    if (!tok->value || tok->length == 0) return none;

    switch (tok->type_) {

    case TOK_EOF:
    case TOK_ERROR:
    case TOK_WHITESPACE:
    case TOK_LPAREN:
    case TOK_RPAREN:
    case TOK_LBRACE:
    case TOK_RBRACE:
    case TOK_LBRACKET:
    case TOK_RBRACKET:
    case TOK_SEMICOLON:
    case TOK_COMMA:
    case TOK_AT:
    case TOK_COLON:
    case TOK_QUESTION:
    case TOK_BANG:
    case TOK_SYMBOL:
        return none;

    case TOK_COMMENT:
    case TOK_DOC_COMMENT:
    case TOK_MODULE_DOC:
    case TOK_DOCUMENT:
        return (TokenClass){ ST_COMMENT, 0 };

    case TOK_STRING:
        return (TokenClass){ ST_STRING, 0 };
    case TOK_CHAR_LITERAL:
        return (TokenClass){ ST_STRING, 0 };

    case TOK_NUMBER:
        return (TokenClass){ ST_NUMBER, 0 };
    case TOK_NUM_FLOAT:
        return (TokenClass){ ST_NUMBER, 0 };

    case TOK_INT:
    case TOK_DOUBLE:
    case TOK_UINT:
    case TOK_FLOAT:
    case TOK_BOOL:
    case TOK_STRINGT:
    case TOK_VOID:
    case TOK_CHAR:
        return (TokenClass){ ST_TYPE, SM_DEFAULT_LIB };

    case TOK_IF:
    case TOK_ELIF:
    case TOK_ELSE:
    case TOK_LOOP:
    case TOK_RETURN:
    case TOK_BREAK:
    case TOK_CONTINUE:
    case TOK_SWITCH:
    case TOK_DEFER:
    case TOK_IN:
        return (TokenClass){ ST_KEYWORD, 0 };

    case TOK_VAR:        /* let */
    case TOK_CONST:
    case TOK_PUBLIC:     /* pub */
    case TOK_PRIVATE:    /* priv */
    case TOK_IMPL:
        return (TokenClass){ ST_KEYWORD, SM_STATIC };

    case TOK_FN:
        return (TokenClass){ ST_KEYWORD, 0 };
    case TOK_STRUCT:
        return (TokenClass){ ST_KEYWORD, 0 };
    case TOK_ENUM:
        return (TokenClass){ ST_KEYWORD, 0 };

    case TOK_MODULE:   /* @module */
    case TOK_USE:      /* @use    */
    case TOK_IMPORT:
    case TOK_AS:
        return (TokenClass){ ST_KEYWORD, SM_STATIC };

    case TOK_PRINT:    /* output   */
    case TOK_PRINTLN:  /* outputln */
    case TOK_INPUT:
    case TOK_ALLOC:
    case TOK_FREE:
    case TOK_SYSTEM:
    case TOK_SYSCALL:
        return (TokenClass){ ST_FUNCTION, SM_DEFAULT_LIB };

    case TOK_CAST:
    case TOK_SIZE_OF:
        return (TokenClass){ ST_FUNCTION, SM_DEFAULT_LIB };

    /* ------------------------------------------------------------------
     * Boolean / nil literals
     * ---------------------------------------------------------------- */
    case TOK_TRUE:
    case TOK_FALSE:
        return (TokenClass){ ST_ENUM_MEMBER, SM_READONLY | SM_DEFAULT_LIB };

    /* ------------------------------------------------------------------
     * Ownership attributes
     * ---------------------------------------------------------------- */
    case TOK_RETURNES_OWNERSHIP:
    case TOK_TAKES_OWNERSHIP:
        return (TokenClass){ ST_MODIFIER, SM_DEFAULT_LIB };

    case TOK_EQUAL:        /* =   */
    case TOK_PLUS:         /* +   */
    case TOK_MINUS:        /* -   */
    case TOK_STAR:         /* *   */
    case TOK_SLASH:        /* /   */
    case TOK_MODL:         /* %   */
    case TOK_LT:           /* <   */
    case TOK_GT:           /* >   */
    case TOK_LE:           /* <=  */
    case TOK_GE:           /* >=  */
    case TOK_EQEQ:         /* ==  */
    case TOK_NEQ:          /* !=  */
    case TOK_AMP:          /* &   */
    case TOK_PIPE:         /* |   */
    case TOK_CARET:        /* ^   */
    case TOK_TILDE:        /* ~   */
    case TOK_AND:          /* &&  */
    case TOK_OR:           /* ||  */
    case TOK_PLUSPLUS:     /* ++  */
    case TOK_MINUSMINUS:   /* --  */
    case TOK_SHIFT_LEFT:   /* <<  */
    case TOK_SHIFT_RIGHT:  /* >>  */
    case TOK_RANGE:        /* ..  */
    case TOK_RIGHT_ARROW:  /* ->  */
    case TOK_LEFT_ARROW:   /* <-  */
    case TOK_DOT:          /* .   */
    case TOK_RESOLVE:      /* ::  */
        return (TokenClass){ ST_OPERATOR, 0 };

    case TOK_KEYWORD:      /* generic keyword catch-all from lexer */
        return (TokenClass){ ST_KEYWORD, 0 };

    case TOK_IDENTIFIER: {
        /* Copy value into a NUL-terminated buffer */
        char word[256];
        size_t len = (size_t)tok->length < 255 ? (size_t)tok->length : 255;
        memcpy(word, tok->value, len);
        word[len] = '\0';

        /* 1. Scope-aware lookup — most precise classification */
        if (doc->scope) {
            Symbol *sym = scope_lookup(doc->scope, word);
            if (sym && sym->type) {
                switch (sym->type->type) {
                case AST_TYPE_FUNCTION:
                    /* Followed by '(' → active call site */
                    if (tok_idx + 1 < doc->token_count &&
                        doc->tokens[tok_idx + 1].type_ == TOK_LPAREN) {
                        return (TokenClass){ ST_FUNCTION,
                                             sym->is_public ? SM_DECLARATION : 0 };
                    }
                    /* Reference to a function value */
                    return (TokenClass){ ST_FUNCTION, 0 };

                case AST_TYPE_STRUCT:
                    return (TokenClass){ ST_STRUCT,
                                         SM_DEFINITION |
                                         (sym->is_public ? SM_DECLARATION : 0) };

                default:
                    /* Enum variants by convention start with uppercase */
                    if (word[0] >= 'A' && word[0] <= 'Z')
                        return (TokenClass){ ST_ENUM_MEMBER, SM_READONLY };

                    return (TokenClass){ ST_VARIABLE,
                                         sym->is_mutable ? 0 : SM_READONLY };
                }
            }
        }

        /* 2. Heuristics for identifiers the scope didn't resolve
         *    (e.g. during a partial/failed parse) */

        /* PascalCase → type reference */
        if (word[0] >= 'A' && word[0] <= 'Z')
            return (TokenClass){ ST_TYPE, 0 };

        /* Immediately followed by '(' → function call */
        if (tok_idx + 1 < doc->token_count &&
            doc->tokens[tok_idx + 1].type_ == TOK_LPAREN)
            return (TokenClass){ ST_FUNCTION, 0 };

        /* Immediately followed by '::' → module/namespace qualifier */
        if (tok_idx + 1 < doc->token_count &&
            doc->tokens[tok_idx + 1].type_ == TOK_RESOLVE)
            return (TokenClass){ ST_NAMESPACE, 0 };

        /* Default: plain variable */
        return (TokenClass){ ST_VARIABLE, 0 };
    }

    default:
        return none;
    }
}

char *lsp_semantic_tokens_full(LSPDocument *doc, ArenaAllocator *arena) {
    if (!doc || !doc->tokens || doc->token_count == 0) return NULL;

    fprintf(stderr, "[LSP] semanticTokens: encoding %zu tokens\n",
            doc->token_count);

    /* Use malloc/realloc so we're not constrained by the arena's remaining
     * space — large files with hundreds of tokens can easily exceed a 64KB
     * temp_arena.  We free data_buf before returning and copy the final
     * result into the arena. */
    size_t data_buf_size = doc->token_count * 64 + 64;
    char  *data_buf = (char *)malloc(data_buf_size);
    if (!data_buf) return NULL;

    size_t  offset      = 0;
    int     prev_line   = 0;
    int     prev_char   = 0;
    size_t  token_count = 0;

    for (size_t i = 0; i < doc->token_count; i++) {
        Token *tok = &doc->tokens[i];
        TokenClass cls = classify_token(doc, i);

        if (cls.type < 0) continue; /* skip — punctuation, whitespace, etc. */

        /* Grow buffer if needed (shouldn't happen with *64 sizing, but safe) */
        if (offset + 80 > data_buf_size) {
            data_buf_size *= 2;
            char *tmp = (char *)realloc(data_buf, data_buf_size);
            if (!tmp) { free(data_buf); return NULL; }
            data_buf = tmp;
        }

        /* LSP positions are 0-based; Luma tokens are 1-based */
        int line = (int)tok->line - 1;
        int col  = (int)tok->col  - 1;
        if (line < 0) line = 0;
        if (col  < 0) col  = 0;

        /* LSP delta encoding:
         *   deltaLine = line - prev_line
         *   deltaChar = col - prev_char  ONLY when on the same line,
         *               otherwise = col (absolute on the new line)      */
        int delta_line = line - prev_line;
        int delta_char = (delta_line == 0) ? (col - prev_char) : col;

        /* Negative deltas mean tokens are out of order — skip rather than
           corrupt the stream.  Happens for synthetic/EOF tokens at 0,0. */
        if (delta_line < 0 || (delta_line == 0 && delta_char < 0)) {
            fprintf(stderr,
                    "[LSP] semanticTokens: skipping out-of-order token "
                    "'%.*s' at %d:%d (prev %d:%d)\n",
                    tok->length, tok->value, line, col, prev_line, prev_char);
            continue;
        }

        if (token_count > 0) {
            data_buf[offset++] = ',';
        }
        offset += (size_t)snprintf(data_buf + offset,
                                   data_buf_size - offset,
                                   "%d,%d,%d,%d,%d",
                                   delta_line, delta_char,
                                   (int)tok->length,
                                   cls.type,
                                   cls.mods);

        prev_line = line;
        prev_char = col;
        token_count++;
    }

    fprintf(stderr, "[LSP] semanticTokens: emitting %zu classified tokens\n",
            token_count);

    /* Copy into arena and free the heap buffer */
    size_t result_size = offset + 16;
    char  *result = arena_alloc(arena, result_size, 1);
    if (result) {
        snprintf(result, result_size, "{\"data\":[%s]}", data_buf);
    }
    free(data_buf);
    return result;
}

const char *lsp_semantic_tokens_capabilities(void) {
    return
        "\"semanticTokensProvider\":{"
            "\"legend\":{"
                "\"tokenTypes\":["
                    "\"namespace\","      /* 0  */
                    "\"type\","           /* 1  */
                    "\"typeParameter\"," /* 2  */
                    "\"function\","       /* 3  */
                    "\"method\","         /* 4  */
                    "\"property\","       /* 5  */
                    "\"variable\","       /* 6  */
                    "\"parameter\","      /* 7  */
                    "\"keyword\","        /* 8  */
                    "\"modifier\","       /* 9  */
                    "\"comment\","        /* 10 */
                    "\"string\","         /* 11 */
                    "\"number\","         /* 12 */
                    "\"operator\","       /* 13 */
                    "\"struct\","         /* 14 */
                    "\"enum\","           /* 15 */
                    "\"enumMember\""      /* 16 */
                "],"
                "\"tokenModifiers\":["
                    "\"declaration\","   /* bit 0 */
                    "\"definition\","    /* bit 1 */
                    "\"readonly\","      /* bit 2 */
                    "\"static\","        /* bit 3 */
                    "\"defaultLibrary\"" /* bit 4 */
                "]"
            "},"
            "\"full\":true"
        "}";
}