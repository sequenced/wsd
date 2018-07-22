#include <string.h>
#include <assert.h>
#include "uri.h"

int
GIVEN_invalid_input_WHEN_parsing_THEN_return_error()
{
     uri_t uri;
     memset((void*)&uri, 0, sizeof(uri));
     assert(-1 == parse_uri("", &uri));
     assert(-1 == parse_uri(":", &uri));
     assert(-1 == parse_uri(":bar", &uri));
     assert(-1 == parse_uri("foo", &uri));
     assert(-1 == parse_uri("foo:", &uri));
     assert(-1 == parse_uri("foo:/", &uri));
     assert(-1 == parse_uri("foo://", &uri));
     assert(-1 == parse_uri("foo://bar:", &uri));
     return 0;
}

int
GIVEN_valid_input_WHEN_parsing_THEN_return_success()
{
     uri_t uri;
     memset((void*)&uri, 0, sizeof(uri));
     assert(0 == parse_uri("ws://foobar:89/", &uri));
     assert(2 == uri.scheme.len);
     assert(0 == strncmp("ws", uri.scheme.p, uri.scheme.len));
     assert(6 == uri.host.len);
     assert(0 == strncmp("foobar", uri.host.p, uri.host.len));
     assert(2 == uri.port.len);
     assert(0 == strncmp("89", uri.port.p, uri.port.len));

     memset((void*)&uri, 0, sizeof(uri));
     assert(0 == parse_uri("ws://foobar", &uri));
     assert(2 == uri.scheme.len);
     assert(0 == strncmp("ws", uri.scheme.p, uri.scheme.len));
     assert(6 == uri.host.len);
     assert(0 == strncmp("foobar", uri.host.p, uri.host.len));
     assert(0 == uri.port.len);
     assert(NULL == uri.port.p);

     return 0;
}

int
main()
{
     assert(0 == GIVEN_invalid_input_WHEN_parsing_THEN_return_error());
     assert(0 == GIVEN_valid_input_WHEN_parsing_THEN_return_success());
     return 0;
}
