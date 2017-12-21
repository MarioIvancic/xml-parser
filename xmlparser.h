// xmlparser.h

#ifndef __XMLPARSER_H__
#define __XMLPARSER_H__

// TODO: napraviti copmile time opciju da se umjesto niza pointera na
// handler funkcije koristi samo jedan pointer za jednu handler funkciju
// drugu opciju da se umjesto niza pointera na handler funkcije koristi
// niz handler funkcija sa predefinisanim imenom koje bi bile implementirane
// u klijentskom kodu
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
    union
    {
        int pool_size;
        int errorcode;
    };
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
enum
{
    XML_ERROR_NONE = 0,
    XML_ERROR_DOCUMENT_END,
    XML_ERROR_NO_MEMORY,
    XML_ERROR_MALFORMED,
};


// register handler
int xml_set_handler(xml_parser_t *p, void *handler, int handler_type);

void xml_parse_string(xml_parser_t* p, char* string);

void xml_init(xml_parser_t* p, char* pool, int pool_size);

void xml_reset(xml_parser_t* p);

#endif // __XMLPARSER_H__
