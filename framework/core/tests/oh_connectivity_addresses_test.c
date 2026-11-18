/* Build with the same OH and JNI headers as oh_connectivity_state.c. */
#define OH_NetConn_GetConnectionProperties fixture_link_properties
#include "../jni/oh_connectivity_state.c"
#include <assert.h>

static int fixture_status;

int32_t fixture_link_properties(NetConn_NetHandle *handle, NetConn_ConnectionProperties *props)
{
    (void)handle;
    if (fixture_status != 0) return fixture_status;
    props->dnsList[0] = (NetConn_NetAddr){.family = 1, .address = "192.0.2.53"};
    props->dnsList[1] = (NetConn_NetAddr){.family = 1, .address = "invalid"};
    props->dnsList[2] = (NetConn_NetAddr){.family = 2, .address = "2001:db8::53"};
    props->dnsList[3] = (NetConn_NetAddr){.family = AF_INET, .address = "198.51.100.53"};
    props->dnsList[4] = (NetConn_NetAddr){.family = AF_INET6, .address = "2001:db8:1::53"};
    props->dnsListSize = 5;
    props->netAddrList[0] = (NetConn_NetAddr){.family = 1, .address = "192.0.2.1", .prefixlen = 24};
    props->netAddrListSize = 1;
    props->routeList[0].destination = (NetConn_NetAddr){.family = 2, .address = "::"};
    props->routeList[0].gateway = (NetConn_NetAddr){.family = 2, .address = "2001:db8::1"};
    props->routeList[0].hasGateway = 1;
    props->routeListSize = 1;
    return 0;
}

int main(void)
{
    NetConn_NetHandle handle = {0};
    NetConn_ConnectionProperties *props = calloc(1, sizeof(*props));
    assert(props);
    assert(get_oh_link_properties(&handle, props) == 0);
    char wire[256];
    assert(props->dnsListSize == 4);
    assert(numeric_address_wire(wire, sizeof(wire), props->dnsList, props->dnsListSize, 0) == 4);
    assert(strcmp(wire, "192.0.2.53;2001:db8::53;198.51.100.53;2001:db8:1::53") == 0);
    assert(props->netAddrList[0].family == AF_INET);
    assert(props->routeList[0].destination.family == AF_INET6);
    assert(props->routeList[0].gateway.family == AF_INET6);
    fixture_status = 201;
    assert(get_oh_link_properties(&handle, props) == fixture_status);
    free(props);
    return 0;
}
