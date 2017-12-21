/*  Copyright (c) 2013, Mario Ivancic
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
    ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// xmlparser.h

#ifndef __XMLPARSER_H__
#define __XMLPARSER_H__

// TODO: napraviti copmile time opciju da se umjesto niza pointera na
// handler funkcije koristi samo jedan pointer za jednu handler funkciju
// drugu opciju da se umjesto niza pointera na handler funkcije koristi
// niz handler funkcija sa predefinisanim imenom koje bi bile implementirane
// u klijentskom kodu

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xml_parser_s xml_parser_t;

struct xml_parser_s
{
    void* user_ptr;
    char* src;
    union
    {
        char* tag;
        char* chars;
        char* errorstr;
        char* comment;
        char* pi;
        char* cdata;
    };
    char* attr;
    char* pool;
    char* _pool;
    int pool_size;
    int errorcode;
    int _pool_size;
    int state;
    int level;
    int (*get_char)(xml_parser_t* p);
    void (*error_handler)(xml_parser_t* p);
    void (*comment_handler)(xml_parser_t* p);
    void (*pi_handler)(xml_parser_t* p);
    void (*cdata_handler)(xml_parser_t* p);
    void (*start_element_handler)(xml_parser_t* p);
    void (*end_element_handler)(xml_parser_t* p);
    void (*characters_handler)(xml_parser_t* p);
};



// handler type values
enum
{
    XML_ERROR_HANDLER = 0,
    XML_COMMENT_HANDLER,
//    XML_START_DOCUMENT_HANDLER,
//    XML_END_DOCUMENT_HANDLER,
    XML_START_ELEMENT_HANDLER,
    XML_END_ELEMENT_HANDLER,
    XML_CHARACTER_HANDLER,
    XML_PI_HANDLER,
    XML_CDATA_HANDLER,
};

// parser error codes
// error codes >= XML_ERROR_USERSTART are user defined
enum
{
    XML_ERROR_NONE = 0,
    XML_ERROR_DOCUMENT_END,
    XML_ERROR_NO_MEMORY,
    XML_ERROR_MALFORMED,
    XML_ERROR_USERSTART,
};


// register handler
int xml_set_handler(xml_parser_t *p, void *handler, int handler_type);

void xml_parse_string(xml_parser_t* p, char* string);

void xml_init(xml_parser_t* p, char* pool, int pool_size);

void xml_reset(xml_parser_t* p);

// helper function for finding attribute in attribute string
int xml_find_attr(const char* attr_string, const char* attr_name, char** attr_val);

// helper function for setting error code from user code
void xml_set_error(xml_parser_t* p, int err_code, const char* err_string);

#ifdef __cplusplus
}
#endif

#endif // __XMLPARSER_H__
