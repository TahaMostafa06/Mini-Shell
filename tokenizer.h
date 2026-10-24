#ifndef tokenizer_h
#define tokenizer_h 


#include <string>
#include <vector>

enum TokenType
{
    TOKEN_COMMAND,    
    TOKEN_ARGUMENT,   
    TOKEN_REDIRECT,  
    TOKEN_APPEND,   
    TOKEN_INPUT,     
    TOKEN_ERROR,    
    TOKEN_PIPE,       
    TOKEN_BACKGROUND, 
    TOKEN_REDIRECT_AND_ERROR,
    TOKEN_EOF,       
};

struct Token
{
    TokenType type;
    std::string value;
};

std::vector<Token> tokenize(const std::string &input);

#endif
