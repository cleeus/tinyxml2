#include "tinyxml2.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

using namespace tinyxml2;

static const char LINE_FEED				= (char)0x0a;			// all line endings are normalized to LF
static const char LF = LINE_FEED;
static const char CARRIAGE_RETURN		= (char)0x0d;			// CR gets filtered out
static const char CR = CARRIAGE_RETURN;
static const char SINGLE_QUOTE			= '\'';
static const char DOUBLE_QUOTE			= '\"';

struct Entity {
	const char* pattern;
	int length;
	char value;
};

static const int NUM_ENTITIES = 5;
static const Entity entities[NUM_ENTITIES] = 
{
	{ "quot", 4,	DOUBLE_QUOTE },
	{ "amp", 3,		'&'  },
	{ "apos", 4,	SINGLE_QUOTE },
	{ "lt",	2, 		'<'	 },
	{ "gt",	2,		'>'	 }
};


const char* StrPair::GetStr()
{
	if ( flags & NEEDS_FLUSH ) {
		*end = 0;
		flags ^= NEEDS_FLUSH;

		if ( flags ) {
			char* p = start;
			char* q = start;

			while( p < end ) {
				if ( (flags & NEEDS_NEWLINE_NORMALIZATION) && *p == CR ) {
					// CR-LF pair becomes LF
					// CR alone becomes LF
					// LF-CR becomes LF
					if ( *(p+1) == LF ) {
						p += 2;
					}
					else {
						++p;
					}
					*q = LF;
				}
				else if ( (flags & NEEDS_NEWLINE_NORMALIZATION) && *p == LF ) {
					if ( *(p+1) == CR ) {
						p += 2;
					}
					else {
						++p;
					}
					*q = LF;
				}
				else if ( (flags & NEEDS_ENTITY_PROCESSING) && *p == '&' ) {
					int i=0;
					for( i=0; i<NUM_ENTITIES; ++i ) {
						if (    strncmp( p+1, entities[i].pattern, entities[i].length ) == 0
							 && *(p+entities[i].length+1) == ';' ) 
						{
							// Found an entity convert;
							*q = entities[i].value;
							++q;
							p += entities[i].length + 2;
							break;
						}
					}
					if ( i == NUM_ENTITIES ) {
						// fixme: treat as error?
						++p;
						++q;
					}
				}
				else {
					*q = *p;
					++p;
					++q;
				}
			}
			*q = 0;
		}
		flags = 0;
	}
	return start;
}


// --------- XMLBase ----------- //
// fixme: should take in the entity/newline flags as param
char* XMLBase::ParseText( char* p, StrPair* pair, const char* endTag, int strFlags )
{
	TIXMLASSERT( endTag && *endTag );

	char* start = p;
	char  endChar = *endTag;
	int   length = strlen( endTag );	

	// Inner loop of text parsing.
	while ( *p ) {
		if ( *p == endChar && strncmp( p, endTag, length ) == 0 ) {
			pair->Set( start, p, strFlags );
			return p + length;
		}
		++p;
	}	
	return p;
}


char* XMLBase::ParseName( char* p, StrPair* pair )
{
	char* start = p;

	start = p;
	if ( !start || !(*start) ) {
		return 0;
	}

	if ( !IsAlpha( *p ) ) {
		return 0;
	}

	while( *p && (
			   IsAlphaNum( (unsigned char) *p ) 
			|| *p == '_'
			|| *p == '-'
			|| *p == '.'
			|| *p == ':' ))
	{
		++p;
	}

	if ( p > start ) {
		pair->Set( start, p, 0 );
		return p;
	}
	return 0;
}


char* XMLBase::Identify( XMLDocument* document, char* p, XMLNode** node ) 
{
	XMLNode* returnNode = 0;
	char* start = p;
	p = XMLNode::SkipWhiteSpace( p );
	if( !p || !*p )
	{
		return 0;
	}

	// What is this thing? 
	// - Elements start with a letter or underscore, but xml is reserved.
	// - Comments: <!--
	// - Decleration: <?xml
	// - Everthing else is unknown to tinyxml.
	//

	static const char* xmlHeader		= { "<?xml" };
	static const char* commentHeader	= { "<!--" };
	static const char* dtdHeader		= { "<!" };
	static const char* cdataHeader		= { "<![CDATA[" };
	static const char* elementHeader	= { "<" };	// and a header for everything else; check last.

	static const int xmlHeaderLen		= 5;
	static const int commentHeaderLen	= 4;
	static const int dtdHeaderLen		= 2;
	static const int cdataHeaderLen		= 9;
	static const int elementHeaderLen	= 1;

	if ( StringEqual( p, commentHeader, commentHeaderLen ) ) {
		returnNode = new XMLComment( document );
		p += commentHeaderLen;
	}
	else if ( StringEqual( p, elementHeader, elementHeaderLen ) ) {
		returnNode = new XMLElement( document );
		p += elementHeaderLen;
	}
	// fixme: better text detection
	else if ( (*p != '<') && IsAlphaNum( *p ) ) {
		// fixme: this is filtering out empty text...should it?
		returnNode = new XMLText( document );
		p = start;	// Back it up, all the text counts.
	}
	else {
		TIXMLASSERT( 0 );
	}

	*node = returnNode;
	return p;
}


// --------- XMLNode ----------- //

XMLNode::XMLNode( XMLDocument* doc ) :
	document( doc ),
	parent( 0 ),
	isTextParent( false ),
	firstChild( 0 ), lastChild( 0 ),
	prev( 0 ), next( 0 )
{

}


XMLNode::~XMLNode()
{
	ClearChildren();
	if ( parent ) {
		parent->Unlink( this );
	}
}


void XMLNode::ClearChildren()
{
	while( firstChild ) {
		XMLNode* node = firstChild;
		Unlink( node );
		delete node;
	}
	firstChild = lastChild = 0;
}


void XMLNode::Unlink( XMLNode* child )
{
	TIXMLASSERT( child->parent == this );
	if ( child == firstChild ) 
		firstChild = firstChild->next;
	if ( child == lastChild ) 
		lastChild = lastChild->prev;

	if ( child->prev ) {
		child->prev->next = child->next;
	}
	if ( child->next ) {
		child->next->prev = child->prev;
	}
	child->parent = 0;
}


XMLNode* XMLNode::InsertEndChild( XMLNode* addThis )
{
	if ( lastChild ) {
		TIXMLASSERT( firstChild );
		TIXMLASSERT( lastChild->next == 0 );
		lastChild->next = addThis;
		addThis->prev = lastChild;
		lastChild = addThis;

		addThis->parent = this;
		addThis->next = 0;
	}
	else {
		TIXMLASSERT( firstChild == 0 );
		firstChild = lastChild = addThis;

		addThis->parent = this;
		addThis->prev = 0;
		addThis->next = 0;
	}
	if ( addThis->ToText() ) {
		SetTextParent();
	}
	return addThis;
}


void XMLNode::Print( XMLStreamer* streamer )
{
	for( XMLNode* node = firstChild; node; node=node->next ) {
		node->Print( streamer );
	}
}


char* XMLNode::ParseDeep( char* p )
{
	while( p && *p ) {
		XMLNode* node = 0;
		p = Identify( document, p, &node );
		if ( p && node ) {
			p = node->ParseDeep( p );
			// FIXME: is it the correct closing element?
			if ( node->IsClosingElement() ) {
				delete node;
				return p;
			}
			this->InsertEndChild( node );
		}
	}
	return 0;
}

// --------- XMLText ---------- //
char* XMLText::ParseDeep( char* p )
{
	p = ParseText( p, &value, "<", StrPair::TEXT_ELEMENT );
	// consumes the end tag.
	if ( p && *p ) {
		return p-1;
	}
	return 0;
}


void XMLText::Print( XMLStreamer* streamer )
{
	const char* v = value.GetStr();
	streamer->PushText( v );
}


// --------- XMLComment ---------- //

XMLComment::XMLComment( XMLDocument* doc ) : XMLNode( doc )
{
}


XMLComment::~XMLComment()
{
	//printf( "~XMLComment\n" );
}


void XMLComment::Print( XMLStreamer* streamer )
{
//	XMLNode::Print( fp, depth );
//	fprintf( fp, "<!--%s-->\n", value.GetStr() );
	streamer->PushComment( value.GetStr() );
}


char* XMLComment::ParseDeep( char* p )
{
	// Comment parses as text.
	return ParseText( p, &value, "-->", StrPair::COMMENT );
}


// --------- XMLAttribute ---------- //
char* XMLAttribute::ParseDeep( char* p )
{
	p = ParseText( p, &name, "=", StrPair::ATTRIBUTE_NAME );
	if ( !p || !*p ) return 0;

	char endTag[2] = { *p, 0 };
	++p;
	p = ParseText( p, &value, endTag, StrPair::ATTRIBUTE_VALUE );
	if ( value.Empty() ) return 0;
	return p;
}


void XMLAttribute::Print( XMLStreamer* streamer )
{
	// fixme: sort out single vs. double quote
	//fprintf( cfile, "%s=\"%s\"", name.GetStr(), value.GetStr() );
	streamer->PushAttribute( name.GetStr(), value.GetStr() );
}


// --------- XMLElement ---------- //
XMLElement::XMLElement( XMLDocument* doc ) : XMLNode( doc ),
	closing( false ),
	rootAttribute( 0 ),
	lastAttribute( 0 )
{
}


XMLElement::~XMLElement()
{
	//printf( "~XMLElemen %x\n",this );

	XMLAttribute* attribute = rootAttribute;
	while( attribute ) {
		XMLAttribute* next = attribute->next;
		delete attribute;
		attribute = next;
	}
}


char* XMLElement::ParseAttributes( char* p, bool* closedElement )
{
	const char* start = p;
	*closedElement = false;

	// Read the attributes.
	while( p ) {
		p = SkipWhiteSpace( p );
		if ( !p || !(*p) ) {
			document->SetError( XMLDocument::ERROR_PARSING_ELEMENT, start, name.GetStr() );
			return 0;
		}

		// attribute.
		if ( IsAlpha( *p ) ) {
			XMLAttribute* attrib = new XMLAttribute( this );
			p = attrib->ParseDeep( p );
			if ( !p ) {
				delete attrib;
				document->SetError( XMLDocument::ERROR_PARSING_ATTRIBUTE, start, p );
				return 0;
			}
			if ( rootAttribute ) {
				TIXMLASSERT( lastAttribute );
				lastAttribute->next = attrib;
				lastAttribute = attrib;
			}
			else {
				rootAttribute = lastAttribute = attrib;
			}
		}
		// end of the tag
		else if ( *p == '/' && *(p+1) == '>' ) {
			if ( closing ) {
				document->SetError( XMLDocument::ERROR_PARSING_ELEMENT, start, p );
				return 0;
			}
			*closedElement = true;
			return p+2;	// done; sealed element.
		}
		// end of the tag
		else if ( *p == '>' ) {
			++p;
			break;
		}
		else {
			document->SetError( XMLDocument::ERROR_PARSING_ELEMENT, start, p );
			return 0;
		}
	}
	return p;
}


//
//	<ele></ele>
//	<ele>foo<b>bar</b></ele>
//
char* XMLElement::ParseDeep( char* p )
{
	// Read the element name.
	p = SkipWhiteSpace( p );
	if ( !p ) return 0;
	const char* start = p;

	// The closing element is the </element> form. It is
	// parsed just like a regular element then deleted from
	// the DOM.
	if ( *p == '/' ) {
		closing = true;
		++p;
	}

	p = ParseName( p, &name );
	if ( name.Empty() ) return 0;

	bool elementClosed=false;
	p = ParseAttributes( p, &elementClosed );
	if ( !p || !*p || elementClosed || closing ) 
		return p;

	p = XMLNode::ParseDeep( p );
	return p;
}


void XMLElement::Print( XMLStreamer* streamer )
{
	//if ( !parent || !parent->IsTextParent() ) {
	//	PrintSpace( cfile, depth );
	//}
	//fprintf( cfile, "<%s", Name() );
	streamer->OpenElement( Name(), IsTextParent() );

	for( XMLAttribute* attrib=rootAttribute; attrib; attrib=attrib->next ) {
		//fprintf( cfile, " " );
		attrib->Print( streamer );

	}

	for( XMLNode* node=firstChild; node; node=node->next ) {
		node->Print( streamer );
	}
	streamer->CloseElement();
}


// --------- XMLDocument ----------- //
XMLDocument::XMLDocument() :
	XMLNode( 0 ),
	charBuffer( 0 )
{
	document = this;	// avoid warning about 'this' in initializer list
}


XMLDocument::~XMLDocument()
{
	delete [] charBuffer;
}


void XMLDocument::InitDocument()
{
	errorID = NO_ERROR;
	errorStr1 = 0;
	errorStr2 = 0;

	delete [] charBuffer;
	charBuffer = 0;

}


int XMLDocument::Parse( const char* p )
{
	ClearChildren();
	InitDocument();

	if ( !p || !*p ) {
		return true;	// correctly parse an empty string?
	}
	size_t len = strlen( p );
	charBuffer = new char[ len+1 ];
	memcpy( charBuffer, p, len+1 );
	XMLNode* node = 0;
	
	char* q = ParseDeep( charBuffer );
	return errorID;
}


void XMLDocument::Print( XMLStreamer* streamer ) 
{
	XMLStreamer stdStreamer( stdout );
	if ( !streamer )
		streamer = &stdStreamer;
	for( XMLNode* node = firstChild; node; node=node->next ) {
		node->Print( streamer );
	}
}


void XMLDocument::SetError( int error, const char* str1, const char* str2 )
{
	errorID = error;
	printf( "ERROR: id=%d '%s' '%s'\n", error, str1, str2 );	// fixme: remove
	errorStr1 = str1;
	errorStr2 = str2;
}


StringStack::StringStack()
{
	nPositive = 0;
	mem.Push( 0 );	// start with null. makes later code simpler.
}


StringStack::~StringStack()
{
}


void StringStack::Push( const char* str ) {
	int needed = strlen( str ) + 1;
	char* p = mem.PushArr( needed );
	strcpy( p, str );
	if ( needed > 1 ) 
		nPositive++;
}


const char* StringStack::Pop() {
	TIXMLASSERT( mem.Size() > 1 );
	const char* p = mem.Mem() + mem.Size() - 2;	// end of final string.
	if ( *p ) {
		nPositive--;
	}
	while( *p ) {					// stack starts with a null, don't need to check for 'mem'
		TIXMLASSERT( p > mem.Mem() );
		--p;
	}
	mem.PopArr( strlen(p)+1 );
	return p+1;
}


XMLStreamer::XMLStreamer( FILE* file ) : fp( file ), depth( 0 ), elementJustOpened( false )
{
	for( int i=0; i<ENTITY_RANGE; ++i ) {
		entityFlag[i] = false;
	}
	for( int i=0; i<NUM_ENTITIES; ++i ) {
		TIXMLASSERT( entities[i].value < ENTITY_RANGE );
		if ( entities[i].value < ENTITY_RANGE ) {
			entityFlag[ entities[i].value ] = true;
		}
	}
}


void XMLStreamer::PrintSpace( int depth )
{
	for( int i=0; i<depth; ++i ) {
		fprintf( fp, "    " );
	}
}


void XMLStreamer::PrintString( const char* p )
{
	// Look for runs of bytes between entities to print.
	const char* q = p;

	while ( *q ) {
		if ( *q < ENTITY_RANGE ) {
			// Check for entities. If one is found, flush
			// the stream up until the entity, write the 
			// entity, and keep looking.
			if ( entityFlag[*q] ) {
				while ( p < q ) {
					fputc( *p, fp );
					++p;
				}
				for( int i=0; i<NUM_ENTITIES; ++i ) {
					if ( entities[i].value == *q ) {
						fprintf( fp, "&%s;", entities[i].pattern );
						break;
					}
				}
				++p;
			}
		}
		++q;
	}
	// Flush the remaining string. This will be the entire
	// string if an entity wasn't found.
	if ( q-p > 0 ) {
		fprintf( fp, "%s", p );
	}
}

void XMLStreamer::OpenElement( const char* name, bool textParent )
{
	if ( elementJustOpened ) {
		SealElement();
	}
	if ( text.NumPositive() == 0 ) {
		PrintSpace( depth );
	}
	stack.Push( name );
	text.Push( textParent ? "T" : "" );

	// fixme: can names have entities?
	fprintf( fp, "<%s", name );
	elementJustOpened = true;
	++depth;
}


void XMLStreamer::PushAttribute( const char* name, const char* value )
{
	TIXMLASSERT( elementJustOpened );
	fprintf( fp, " %s=\"", name );
	PrintString( value );
	fprintf( fp, "\"" );
}


void XMLStreamer::CloseElement()
{
	--depth;
	const char* name = stack.Pop();
	int wasPositive = text.NumPositive();
	text.Pop();

	if ( elementJustOpened ) {
		fprintf( fp, "/>" );
		if ( text.NumPositive() == 0 ) {
			fprintf( fp, "\n" );
		}
	}
	else {
		if ( wasPositive == 0 ) {
			PrintSpace( depth );
		}
		// fixme can names have entities?
		fprintf( fp, "</%s>", name );
		if ( text.NumPositive() == 0 ) {
			fprintf( fp, "\n" );
		}
	}
	elementJustOpened = false;
}


void XMLStreamer::SealElement()
{
	elementJustOpened = false;
	fprintf( fp, ">" );
	if ( text.NumPositive() == 0 ) {
		fprintf( fp, "\n" );
	}
}


void XMLStreamer::PushText( const char* text )
{
	if ( elementJustOpened ) {
		SealElement();
	}
	PrintString( text );
}


void XMLStreamer::PushComment( const char* comment )
{
	if ( elementJustOpened ) {
		SealElement();
	}
	PrintSpace( depth );
	fprintf( fp, "<!--%s-->\n", comment );
}
