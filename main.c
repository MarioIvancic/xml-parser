#include <stdio.h>
#include <stdlib.h>
#include "xmlparser.h"

const char* xml_texts[6] =
{
    // string 0
"<?xml version=\"1.0\"?>\n"
"<Document>\n"
"   <Prefix></Prefix>\n"
"   <Element>\n"
"	    <With>Child</With>\n"
"   </Element>\n"
"</Document>",

    // string 1
"<?xml version=\"1.0\"?>\n"
"<network>\n"
"	<animation clip=\"idle\" flags=\"loop\" />\n"
"	<animation clip=\"run\" flags=\"loop\" />\n"
"	<animation clip=\"attack\" />\n"
"\n"
"	<?include transitions.xml?>\n"
"</network>",

    // string 2
"<transition source=\"idle\" target=\"run\">\n"
"	<event name=\"key_up|key_shift\" />\n"
"</transition>\n"
"<transition source=\"run\" target=\"attack\">\n"
"	<event name=\"key_ctrl\" />\n"
"	<condition expr=\"weapon != null\" />\n"
"</transition>",

    // string 3
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n"
"<Profile FormatVersion=\"1\">\n"
"    <Tools>\n"
"        <Tool Filename=\"jam\" AllowIntercept=\"true\">\n"
"        	<Description>Jamplus build system</Description>\n"
"        </Tool>\n"
"        <Tool Filename=\"mayabatch.exe\" AllowRemote=\"true\" OutputFileMasks=\"*.dae\" DeriveCaptionFrom=\"lastparam\" Timeout=\"40\" />\n"
"        <Tool Filename=\"meshbuilder_*.exe\" AllowRemote=\"false\" OutputFileMasks=\"*.mesh\" DeriveCaptionFrom=\"lastparam\" Timeout=\"10\" />\n"
"        <Tool Filename=\"texbuilder_*.exe\" AllowRemote=\"true\" OutputFileMasks=\"*.tex\" DeriveCaptionFrom=\"lastparam\" />\n"
"        <Tool Filename=\"shaderbuilder_*.exe\" AllowRemote=\"true\" DeriveCaptionFrom=\"lastparam\" />\n"
"    </Tools>\n"
"</Profile>",

    // string 4
"<?xml version=\"1.0\"?>\n"
"<config>\n"
"<!--This is a config file for the Irrlicht Engine Mesh Viewer.-->\n"
"<startUpModel file=\"../../media/dwarf.x\" />\n"
"<messageText caption=\"Irrlicht Engine Mesh Viewer\">Welcome to the Mesh Viewer of the &quot;Irrlicht Engine&quot;. This program is able to load and display all 3D geometry and models, the Irrlicht Engine can. \n"
"\n"
"Controls:\n"
"- Left mouse to rotate\n"
"- Right mouse to move\n"
"- Both buttons to zoom\n"
"\n"
"Supported formats are:\n"
"- 3D Studio (.3ds)\n"
"- Cartography shop 4 (.csm)\n"
"- DirectX (.x)\n"
"- Maya (.obj)\n"
"- Milkshape (.ms3d)\n"
"- My3D (.my3D) \n"
"- OCT (.oct)\n"
"- Pulsar LMTools (.lmts)\n"
"- Quake 3 levels (.bsp)\n"
"- Quake 2 models (.md2)\n"
"\n"
"Please note that this program is also a demo of the user interface capabilities of the engine, so for example the combo box in the toolbar has no function.\n"
"</messageText>\n"
"</config>",

    // string 5
"<?xml version=\"1.0\"?>\n"
"<mesh name=\"mesh_root\">\n"
"	<!-- here is a mesh node -->\n"
"	some text\n"
"	<![CDATA[someothertext]]>\n"
"	some more text\n"
"	<node attr1=\"value1\" attr2=\"value2\" />\n"
"	<node attr1=\"value2\">\n"
"		<innernode/>\n"
"	</node>\n"
"</mesh>\n"
"<?include somedata?>",
};




void start_element(xml_parser_t* p)
{
    if(p->attr) printf("Start Element '%s', attributes '%s'\n", p->tag, p->attr);
    else printf("Start Element '%s'\n", p->tag);
}

void end_element(xml_parser_t* p)
{
    printf("End Element '%s'\n", p->tag);
}


void xml_error(xml_parser_t* p)
{
    printf("ERROR %d, (%s)\n", p->errorcode, p->errorstr);
}


void xml_comment(xml_parser_t* p)
{
    printf("Comment '%s'\n", p->comment);
}

void xml_pi(xml_parser_t* p)
{
    printf("PI '%s'\n", p->pi);
}

void xml_cdata(xml_parser_t* p)
{
    printf("CDATA '%s'\n", p->cdata);
}

void xml_chars(xml_parser_t* p)
{
    printf("Chars '%s'\n", p->chars);
}




int main()
{
    int i;

    xml_parser_t xml_parser;
    char pool[1024];

    xml_init(&xml_parser, pool, sizeof(pool));

    xml_set_handler(&xml_parser, xml_error, XML_ERROR_HANDLER);
    xml_set_handler(&xml_parser, xml_comment, XML_COMMENT_HANDLER);
    xml_set_handler(&xml_parser, start_element, XML_START_ELEMENT_HANDLER);
    xml_set_handler(&xml_parser, end_element, XML_END_ELEMENT_HANDLER);
    xml_set_handler(&xml_parser, xml_chars, XML_CHARACTER_HANDLER);
    xml_set_handler(&xml_parser, xml_pi, XML_PI_HANDLER);
    xml_set_handler(&xml_parser, xml_cdata, XML_CDATA_HANDLER);

    for(i = 0; i < sizeof(xml_texts) / sizeof(xml_texts[0]); i++)
    {
        printf("Parsing string %d\n", i);
        xml_parse_string(&xml_parser, xml_texts[i]);
        printf("\n\n\n\n");
    }

    return 0;
}
