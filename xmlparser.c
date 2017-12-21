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

// xmlparser.c

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
//#include <stdio.h>
#include "xmlparser.h"

void log_debug(const char* format, ...);

#define XML_ERROR(code, string) xml_set_error(p, (code), (string))

// macro to update pointers and return
#define RETURN(n) do { p->pool = pool; p->pool_size = pool_size; return (n); } while(0)


/*
    Allowed characters:
    S               [ \r\n\t]
    NameStartChar   [:_A-Za-z]
    NameChar        NameStartChar | [0-9.-]
    Name            NameStartChar NameChar*
    EntityRef       & Name ;
    CharRef         &# [0-9]+ ; | &#x [0-9a-fA-F]+ ;
    Reference       EntityRef | CharRef
    AttValue        " ([^<&"] | Reference)* " | ' ([^<&'] | Reference)* '
    AttName         Name
    Attribute       AttName Eq AttValue
    STag            < Name (S Attribute)* S? >
    ETag            </ Name S? >
    EmptyElemTag    < Name (S Attribute)* S? />

    Note: built-in EntityRef:  amp, lt, gt, apos, quot
*/

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



// get next char from string
// all kinds of line endings converted to '\n' ('\r' ignored in "\r\n", converted to '\n' in "\r")
static int get_xml_char(xml_parser_t* p)
{
    int i = *(p->src);
    p->src++;

    if(i == '\r')
    {
        i = *(p->src);
        if(i == '\n') p->src++;
        else if(!i) i = -1;
        else i = '\n';
    }
    else if(!i) i = -1;

    return i;
}


// generic parser

// after '<' we have to test next char
// returns 1 if we need to stop parsing, 0 otherwise
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
// returns 1 if we need to stop parsing, 0 otherwise
static int xml_parse_cdata(xml_parser_t* p)
{
    char* pool = p->pool;
    int pool_size = p->pool_size;

    // we get here after '<![' so we have to test next few chars
    // and than wait for ']]>'
    static const char cdata_start[] = "CDATA[";
    static const char cdata_end[] = "]]>";
    int i, c;

    for(i = 0; i < 6; i++)
    {
        c = p->get_char(p);
        if(c != cdata_start[i])
        {
            XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
            RETURN(1);
        }
        if(c == -1)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
            RETURN(1);
        }
    }

    // we are now in CDATA block which must end with ']]>'
    i = 0;
    p->cdata = pool;

    while(1)
    {
        c = p->get_char(p);
        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;

        if(c == cdata_end[i])
        {
            i++;
            if(i == 3) // we are done
            {
                p->state = STATE_CHARS;
                // we have to trim ']]>' from the end of pool
                pool[-3] = 0;

                // call cdata handler
                if(p->cdata_handler) p->cdata_handler(p);

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
                RETURN(1);
            }
        }
    }

    RETURN(0);
}



// after first char of attributes we have to parse rest of the chars
// returns 1 if we need to stop parsing, 0 otherwise
static int xml_parse_attributes(xml_parser_t* p)
{
    int c, quote_char;
    char* pool = p->pool;
    int pool_size = p->pool_size;
    char* ref;

    c = p->get_char(p);

parse_name:

    while(c != -1 && c != '=')
    {
        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        RETURN(1);
    }

    // c is now '=' so we have to test next char to see is it ' or "
    if(!pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        RETURN(1);
    }

    *pool++ = c;
    pool_size--;

    c = p->get_char(p);
    if(!pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        RETURN(1);
    }

    *pool++ = c;
    pool_size--;

    if(c == '"' || c == '\'') quote_char = c;
    else
    {
        XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
        RETURN(1);
    }

    // now we have to parse value
    c = p->get_char(p);
    ref = 0;
    while(c != -1 && c != quote_char)
    {
        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;

        // test for reference (&#\d+; &#x\h+; &amp; &lt; &gt; &apos; &quot;)
        if(!ref && c == '&')
        {
            ref = pool;
        }
        else if(ref && c == ';')
        {
            long t = -1;

            *pool = 0;          // terminatin char for reference

            // ref now points to reference after '&' character
            // and ends with ';' character
            if(ref[0] == '#')   // CharRef
            {
                if(ref[1] == 'x')   // hexadecimal CharRef
                {
                    t = strtol(ref + 2, 0, 16);
                }
                else                // decimal CharRef
                {
                    t = strtol(ref + 1, 0, 10);
                }
            }
            else if(ref[0] == 'a')
            {
                if(ref[1] == 'm' && ref[2] == 'p') t = '&';
                else if(ref[1] == 'p' && ref[2] == 'o' && ref[3] == 's') t = '\'';
            }
            else if(ref[0] == 'l' && ref[1] == 't') t = '<';
            else if(ref[0] == 'g' && ref[1] == 't') t = '>';
            else if(ref[0] == 'q' && ref[1] == 'u' && ref[2] == 'o' && ref[3] == 't') t = '"';

            if(t == -1)
            {
                XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
                RETURN(1);
            }
            else
            {
                ref[-1] = t;
                pool_size += (int)(pool - ref);
                pool = ref;
            }

            ref = 0;
        }

        c = p->get_char(p);
    }

    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        RETURN(1);
    }

    if(!pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        RETURN(1);
    }

    *pool++ = c;
    pool_size--;

    // now we have to find new attribute name or end of tag
    c = p->get_char(p);

    // skip all whitespace chars
    while(c == ' ')
    {
        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        RETURN(1);
    }

    // we have to test c to see is it end of tag or new attribute name
    if(c == '/')
    {
        *pool = 0;       // terminating char
        // trim trailing space chars
        pool--;
        while(*pool == ' ') *pool-- = 0;

        // next char should be '>'
        c = p->get_char(p);
        if(c == -1)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
            RETURN(1);
        }

        if(c != '>')
        {
            XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
            RETURN(1);
        }

        p->level++;
        // call start_element_handler
        if(p->start_element_handler) p->start_element_handler(p);

        p->level--;
        // call end_element_handler
        if(p->end_element_handler) p->end_element_handler(p);
    }
    else if(c == '>')
    {
        *pool = 0;       // terminating char
        // trim trailing space chars
        pool--;
        while(*pool == ' ') *pool-- = 0;

        p->level++;
        // call start_element_handler
        if(p->start_element_handler) p->start_element_handler(p);
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



// returns 1 if we need to stop parsing, 0 otherwise
static int xml_parse_comment(xml_parser_t* p)
{
    char* pool = p->pool;
    int pool_size = p->pool_size;

    // we get here after '<!-' so we have to test next char
    // and than wait for '-->'

    int c = p->get_char(p);

    if(c == '-')
    {
        const char comment_end[] = "-->";
        int i;

        // we are now in comment block which must end with '-->'
        i = 0;
        p->comment = pool;

        while(1)
        {
            c = p->get_char(p);
            if(!pool_size)
            {
                XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
                RETURN(1);
            }

            *pool++ = c;
            pool_size--;

            if(c == comment_end[i])
            {
                i++;
                if(i == 3) // we are done
                {
                    //p->state = STATE_START;
                    p->state = STATE_CHARS;
                    // we have to trim '-->' from the end of pool
                    pool[-3] = 0;

                    // call comment handler
                    if(p->comment_handler) p->comment_handler(p);

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
                    RETURN(1);
                }
            }
        }
    }
    else if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        RETURN(1);
    }
    else
    {
        XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
        RETURN(1);
    }

    RETURN(0);
}



// parse processing instructions <?...?>
// returns 1 if we need to stop parsing, 0 otherwise
static int xml_parse_pi(xml_parser_t* p)
{
    int c;
    char* pool = p->pool;
    int pool_size = p->pool_size;

    c = p->get_char(p);
    while(c != -1 && c != '?')
    {
        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        RETURN(1);
    }

    // now we know it's '?', next char should be '>'
    c = p->get_char(p);

    if(c == '>')
    {
        *pool = 0;
        // trim trailing space chars
        pool--;
        while(*pool == ' ') *pool-- = 0;


        // call PI callback
        if(p->pi_handler) p->pi_handler(p);

        // reset memory pool
        p->pool = p->_pool;
        p->pool_size = p->_pool_size;
        p->state = STATE_CHARS;
        p->chars = p->_pool;
        return 0;
    }
    else if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        RETURN(1);
    }
    else
    {
        XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
        RETURN(1);
    }

    RETURN(0);
}



// find start of tag
// returns 1 if we need to stop parsing, 0 otherwise
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



// returns 1 if we need to stop parsing, 0 otherwise
static int xml_parse_tagend(xml_parser_t* p)
{
    char* pool = p->pool;
    int pool_size = p->pool_size;
    int c = p->get_char(p);

    while(c != -1 && c != '>')
    {
        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;
        c = p->get_char(p);
    }

    // end of stream or end of tag name
    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        RETURN(1);
    }

    // now we know c == '>'
    if(!pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        RETURN(1);
    }

    p->attr = 0;        // no attributes
    *pool = 0;       // terminating char

    p->level--;
    // call end_element_handler
    if(p->end_element_handler) p->end_element_handler(p);

    p->state = STATE_CHARS;
    p->chars = p->_pool;

    // reset pool memory
    p->pool = p->_pool;
    p->pool_size = p->_pool_size;

    return 0;
}



// get xml tag
// returns 1 if we need to stop parsing, 0 otherwise
static int xml_parse_tag(xml_parser_t* p)
{
    char* pool = p->pool;
    int pool_size = p->pool_size;
    int c = p->get_char(p);

    while(c != -1 && c != ' ' && c != '>' && c != '/')
    {
        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;
        c = p->get_char(p);
    }

    // skip all whitespace chars
    while(c == ' ') c = p->get_char(p);

    // end of stream or end of tag name
    if(c == -1)
    {
        XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        RETURN(1);
    }

    if(!pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        RETURN(1);
    }

    if(c == '>' || c == '/')
    {
        // it's a tag without attributes
        p->attr = pool;     // no attributes, so p->attr points to null string
        *pool = 0;          // terminating char

        p->level++;
        // call start_element_handler
        if(p->start_element_handler) p->start_element_handler(p);

        if(c == '/')
        {
            // next char should be '>'
            c = p->get_char(p);
            if(c == -1)
            {
                XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
                RETURN(1);
            }

            if(c != '>')
            {
                XML_ERROR(XML_ERROR_MALFORMED, "Malformed xml document");
                RETURN(1);
            }

            p->level--;
            // call end_element_handler
            if(p->end_element_handler) p->end_element_handler(p);
        }

        p->state = STATE_CHARS;
        p->chars = p->_pool;

        // reset pool memory
        pool = p->_pool;
        pool_size = p->_pool_size;
    }
    else
    {
        // it's a tag with attributes
        *pool++ = 0;     // terminating char
        pool_size--;

        // save attributes string
        p->attr = pool;

        // skip all whitespace chars
        while(c == ' ') c = p->get_char(p);

        if(c == -1)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
            RETURN(1);
        }

        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;

        p->state = STATE_ATTR;
    }

    RETURN(0);
}



// returns 1 if we need to stop parsing, 0 otherwise
static int xml_parse_chars(xml_parser_t* p)
{
    char* pool = p->pool;
    int pool_size = p->pool_size;
    int c = p->get_char(p);

    while(c != -1 && c != '<')
    {
        if(!pool_size)
        {
            XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
            RETURN(1);
        }

        *pool++ = c;
        pool_size--;
        c = p->get_char(p);
    }

    if(c == -1)
    {
        if(p->level)
        {
            XML_ERROR(XML_ERROR_DOCUMENT_END, "Premature end of xml document");
        }

        RETURN(1);
    }

    // end of chars
    if(!pool_size)
    {
        XML_ERROR(XML_ERROR_NO_MEMORY, "No enough memory in pool");
        RETURN(1);
    }

    *pool++ = 0;     // terminating char

    // call characters_handler
    if(p->characters_handler) p->characters_handler(p);

    // reset memory pool
    p->pool = p->_pool;
    p->pool_size = p->_pool_size;

    p->state = STATE_TESTLT;

    RETURN(0);
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
    int i = XML_ERROR_NONE;

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

        default: i = XML_ERROR_ARG;
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


// helper function for finding attribute attr_name
// returns value len and pointer to value in attr_val
// return -1 if not found
int xml_find_attr(const char* attr_string, const char* attr_name, char** attr_val)
{
    char* ptr = (char*)attr_string;

    while(1)
    {
        ptr = strstr(ptr, attr_name);
        if(!ptr) return -1;
        ptr += strlen(attr_name);
        if(*ptr == '=')
        {
            ++ptr;
            break;
        }
    }
    if(*ptr == '"')
    {
        *attr_val = ++ptr;

        while(1)
        {
            ptr = strchr(ptr, '"');
            if(!ptr) return -1;
            if(ptr[-1] == '\\') ++ptr;
            else break;
        }
        return (int)(ptr - *attr_val);
    }
    else if(*ptr == '\'')
    {
        *attr_val = ++ptr;

        while(1)
        {
            ptr = strchr(ptr, '\'');
            if(!ptr) return -1;
            if(ptr[-1] == '\\') ++ptr;
            else break;
        }
        return (int)(ptr - *attr_val);
    }

    return -1;
}



void xml_set_error(xml_parser_t* p, int err_code, const char* err_string)
{
    p->tag = (char*)err_string;
    p->errorcode = err_code;
    if(p->error_handler) p->error_handler(p);
}
