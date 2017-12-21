// xmlparser.c

#include <stdint.h>
#include <stdio.h>
#include "xmlparser.h"

#define XML_ERROR(code, string) do { \
        p->tag = (string); \
        p->pool_size = (code); \
        p->error_handler(p); \
    } while(0)

// constants for xml_parser_t::state
enum
{
    STATE_START = 0,
    STATE_PI,
    STATE_TAG,
    STATE_TESTLT,
    STATE_ETAG,
    STATE_CHARS,
    STATE_COMMENT,
    STATE_CDATA,
    STATE_ATTR,
};


static int get_xml_char(xml_parser_t* p)
{
    int i = *(p->src);
    if(!i) return -1;
    p->src++;
    return i;
}


// generic parser

// after '<' we have to test next char
static int xml_parse_testlt(xml_parser_t* p)
{
    // get next char
    int c = p->get_char(p);

    if(c == '/')
    {
        p->state = STATE_ETAG;
        p->tag = p->pool;
    }
    else if(c == '?')
    {
        p->state = STATE_PI;
        p->pi = p->pool;
    }
    else if(c == '!')
    {
        // we need to test next char to see is it comment or CDATA
        c = p->get_char(p);

        if(c == '-') p->state = STATE_COMMENT;
        else if(c == '[') p->state = STATE_CDATA;
        else if(c == -1)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
            return 1;
        }
        else
        {
            XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
            return 1;
        }
    }
    else if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }
    else
    {
        p->state = STATE_TAG;
        p->tag = p->pool;
        *p->pool++ = c;
        p->pool_size--;
    }

    return 0;
}


// after '<![' we have to extract CDATA block ending with ']]>'
static int xml_parse_cdata(xml_parser_t* p)
{
    // we get here after '<![' so we have to test next few chars
    // and than wait for ']]>'
    const char cdata_start[] = "CDATA[";
    const char cdata_end[] = "]]>";
    int i, c;

    for(i = 0; i < 6; i++)
    {
        c = p->get_char(p);
        if(c != cdata_start[i])
        {
            XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
            return 1;
        }
        if(c == -1)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
            return 1;
        }
    }

    // we are now in CDATA block which must end with ']]>'
    i = 0;
    p->cdata = p->pool;

    while(1)
    {
        c = p->get_char(p);
        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;

        if(c == cdata_end[i])
        {
            i++;
            if(i == 3) // we are done
            {
                p->state = STATE_CHARS;
                // we have to trim ']]>' from the end of pool
                p->pool[-3] = 0;

                // call cdata handler
                p->cdata_handler(p);

                // reset pool memory
                p->pool = p->_pool;
                p->pool_size = p->_pool_size;

                p->chars = p->_pool;

                return 0;
            }
        }
        else
        {
            i = 0;
            if(c == -1)
            {
                XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
                return 1;
            }
        }
    }

    return 0;
}



// after first char of attributes we have to parse rest of the chars
static int xml_parse_attributes(xml_parser_t* p)
{
    int c, quote_char;

    c = p->get_char(p);

parse_name:

    while(c != -1 && c != '=')
    {
        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }

    // c is now '=' so we have to test next char to see is it ' or "
    if(!p->pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        return 1;
    }

    *p->pool++ = c;
    p->pool_size--;

    c = p->get_char(p);
    if(!p->pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        return 1;
    }

    *p->pool++ = c;
    p->pool_size--;

    if(c == '"' || c == '\'') quote_char = c;
    else
    {
        XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
        return 1;
    }

    // now we have to parse value
    c = p->get_char(p);
    while(c != -1 && c != quote_char)
    {
        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }

    if(!p->pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        return 1;
    }

    *p->pool++ = c;
    p->pool_size--;

    // now we have to find new attribute name or end of tag
    c = p->get_char(p);

    // skip all whitespace chars
    while(c == ' ')
    {
        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }

    // we have to test c to see is it end of tag or new attribute name
    if(c == '/')
    {
        *p->pool = 0;       // terminating char
        // trim trailing space chars
        p->pool--;
        while(*p->pool == ' ') *p->pool-- = 0;

        // next char should be '>'
        c = p->get_char(p);
        if(c == -1)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
            return 1;
        }

        if(c != '>')
        {
            XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
            return 1;
        }

        p->level++;
        // call start_element_handler
        p->start_element_handler(p);

        p->level--;
        // call end_element_handler
        p->end_element_handler(p);
    }
    else if(c == '>')
    {
        *p->pool = 0;       // terminating char
        // trim trailing space chars
        p->pool--;
        while(*p->pool == ' ') *p->pool-- = 0;

        p->level++;
        // call start_element_handler
        p->start_element_handler(p);
    }
    else
    {
        // new attribute name
        goto parse_name;
    }

    // reset pool memory
    p->pool = p->_pool;
    p->pool_size = p->_pool_size;

    p->state = STATE_CHARS;
    p->chars = p->_pool;

    return 0;
}




static int xml_parse_comment(xml_parser_t* p)
{
    // we get here after '<!-' so we have to test next char
    // and than wait for '-->'

    int c = p->get_char(p);

    if(c == '-')
    {
        const char comment_end[] = "-->";
        int i;

        // we are now in comment block which must end with '-->'
        i = 0;
        p->comment = p->pool;

        while(1)
        {
            c = p->get_char(p);
            if(!p->pool_size)
            {
                XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
                return 1;
            }

            *p->pool++ = c;
            p->pool_size--;

            if(c == comment_end[i])
            {
                i++;
                if(i == 3) // we are done
                {
                    //p->state = STATE_START;
                    p->state = STATE_CHARS;
                    // we have to trim '-->' from the end of pool
                    p->pool[-3] = 0;

                    // call comment handler
                    p->comment_handler(p);

                    // reset pool memory
                    p->pool = p->_pool;
                    p->pool_size = p->_pool_size;

                    p->chars = p->_pool;

                    return 0;
                }
            }
            else
            {
                i = 0;
                if(c == -1)
                {
                    XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
                    return 1;
                }
            }
        }
    }
    else if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }
    else
    {
        XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
        return 1;
    }

    return 0;
}




static int xml_parse_pi(xml_parser_t* p)
{
    int c;

    c = p->get_char(p);
    while(c != -1 && c != '?')
    {
        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }

    // now we know it's '?', next char should be '>'
    c = p->get_char(p);

    if(c == '>')
    {
        *p->pool = 0;
        // trim trailing space chars
        p->pool--;
        while(*p->pool == ' ') *p->pool-- = 0;


        // call PI callback
        p->pi_handler(p);

        // reset memory pool
        p->pool = p->_pool;
        p->pool_size = p->_pool_size;
        p->state = STATE_CHARS;
        p->chars = p->_pool;
    }
    else if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }
    else
    {
        XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
        return 1;
    }

    return 0;
}



// find start of tag
static int xml_parse_start(xml_parser_t* p)
{
    int c;

    // find first '<'
    c = p->get_char(p);
    while(c != -1 && c != '<') c = p->get_char(p);

    // now c is -1 or '<'
    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }

    // now c is '<'
    p->state = STATE_TESTLT;

    return 0;
}



static int xml_parse_tagend(xml_parser_t* p)
{
    int c = p->get_char(p);
    while(c != -1 && c != '>')
    {
        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;
        c = p->get_char(p);
    }

    // end of stream or end of tag name
    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }

    // now we know c == '>'
    if(!p->pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        return 1;
    }

    p->attr = 0;        // no attributes
    *p->pool = 0;       // terminating char

    p->level--;
    // call end_element_handler
    p->end_element_handler(p);

    p->state = STATE_CHARS;
    p->chars = p->_pool;

    // reset pool memory
    p->pool = p->_pool;
    p->pool_size = p->_pool_size;

    return 0;
}



// get xml tag
static int xml_parse_tag(xml_parser_t* p)
{
    int c = p->get_char(p);
    while(c != -1 && c != ' ' && c != '>' && c != '/')
    {
        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;
        c = p->get_char(p);
    }

    // end of stream or end of tag name
    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        return 1;
    }

    if(!p->pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        return 1;
    }

    if(c == '>' || c == '/')
    {
        // it's a tag without attributes
        p->attr = 0;        // no attributes
        *p->pool = 0;       // terminating char

        p->level++;
        // call start_element_handler
        p->start_element_handler(p);

        if(c == '/')
        {
            // next char should be '>'
            c = p->get_char(p);
            if(c == -1)
            {
                XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
                return 1;
            }

            if(c != '>')
            {
                XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
                return 1;
            }

            p->level--;
            // call end_element_handler
            p->end_element_handler(p);
        }

        p->state = STATE_CHARS;
        p->chars = p->_pool;

        // reset pool memory
        p->pool = p->_pool;
        p->pool_size = p->_pool_size;
    }
    else
    {
        // it's a tag with attributes
        *p->pool++ = 0;     // terminating char
        p->pool_size--;

        // save attributes string
        p->attr = p->pool;

        // skip all whitespace chars
        while(c == ' ') c = p->get_char(p);

        if(c == -1)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
            return 1;
        }

        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;

        p->state = STATE_ATTR;
    }

    return 0;
}



static int xml_parse_chars(xml_parser_t* p)
{
    int c = p->get_char(p);
    while(c != -1 && c != '<')
    {
        if(!p->pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            return 1;
        }

        *p->pool++ = c;
        p->pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        if(p->level)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        }

        return 1;
    }

    // end of chars
    if(!p->pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        return 1;
    }

    *p->pool++ = 0;     // terminating char

    // call characters_handler
    p->characters_handler(p);

    // reset memory pool
    p->pool = p->_pool;
    p->pool_size = p->_pool_size;

    p->state = STATE_TESTLT;

    return 0;
}



static void xml_parse(xml_parser_t* p)
{
    int stop = 0;

    p->state = STATE_START;

    while(stop == 0)
    {
             if(p->state == STATE_CHARS)    stop = xml_parse_chars(p);
        else if(p->state == STATE_TESTLT)   stop = xml_parse_testlt(p);
        else if(p->state == STATE_TAG)      stop = xml_parse_tag(p);
        else if(p->state == STATE_ATTR)     stop = xml_parse_attributes(p);
        else if(p->state == STATE_ETAG)     stop = xml_parse_tagend(p);
        else if(p->state == STATE_PI)       stop = xml_parse_pi(p);
        else if(p->state == STATE_COMMENT)  stop = xml_parse_comment(p);
        else if(p->state == STATE_CDATA)    stop = xml_parse_cdata(p);
        else if(p->state == STATE_START)    stop = xml_parse_start(p);
    }
}



void xml_parse_string(xml_parser_t* p, char* string)
{
    p->src = string;
    p->get_char = get_xml_char;

    xml_parse(p);

    xml_reset(p);
}


int xml_set_handler(xml_parser_t *p, void *handler, int handler_type)
{
    int i = 0;

    switch(handler_type)
    {
        case XML_ERROR_HANDLER:
            p->error_handler = handler;
        break;

        case XML_COMMENT_HANDLER:
            p->comment_handler = handler;
        break;

        case XML_START_ELEMENT_HANDLER:
            p->start_element_handler = handler;
        break;

        case XML_END_ELEMENT_HANDLER:
            p->end_element_handler = handler;
        break;

        case XML_CHARACTER_HANDLER:
            p->characters_handler = handler;
        break;

        case XML_PI_HANDLER:
            p->pi_handler = handler;
        break;

        case XML_CDATA_HANDLER:
            p->cdata_handler = handler;
        break;

        default: i = -1;
    }

    return i;
}



void xml_init(xml_parser_t* p, char* pool, int pool_size)
{
    p->pool = pool;
    p->_pool = pool;
    p->pool_size = pool_size;
    p->_pool_size = pool_size;
    p->src = 0;
    p->tag = 0;
    p->attr = 0;
    p->state = 0;
    p->level = 0;
    p->get_char = 0;
    p->error_handler = 0;
    p->comment_handler = 0;
    p->pi_handler = 0;
    p->cdata_handler = 0;
    p->start_element_handler = 0;
    p->end_element_handler = 0;
    p->characters_handler = 0;
}


void xml_reset(xml_parser_t* p)
{
    p->pool = p->_pool;
    p->pool_size = p->_pool_size;
    p->src = 0;
    p->tag = 0;
    p->attr = 0;
    p->state = 0;
    p->level = 0;
    p->get_char = 0;
}
