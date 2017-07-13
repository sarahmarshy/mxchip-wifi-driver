/* MXCHIP implementation of NetworkInterfaceAPI
 * Copyright (c) 2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "MXCHIPInterface.h"

// Various timeouts for different MXCHIP operations
#define MXCHIP_CONNECT_TIMEOUT 15000
#define MXCHIP_SEND_TIMEOUT    500
#define MXCHIP_RECV_TIMEOUT    0
#define MXCHIP_MISC_TIMEOUT    500

// MXCHIPInterface implementation
MXCHIPInterface::MXCHIPInterface(PinName tx, PinName rx, bool debug)
    : _mxchip(tx, rx, debug)
{
    _channel=0;
    memset(_ids, 0, sizeof(_ids));
    memset(_cbs, 0, sizeof(_cbs));

    _mxchip.attach(this, &MXCHIPInterface::event);
}

int MXCHIPInterface::connect(const char *ssid, const char *pass, nsapi_security_t security,uint8_t channel)
{
    set_credentials(ssid, pass, security);
    return connect();
}

int MXCHIPInterface::connect(){
    _mxchip.setTimeout(MXCHIP_CONNECT_TIMEOUT);

    if (!_mxchip.startup()) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    if (!_mxchip.connect(ap_ssid, ap_pass)) {
        return NSAPI_ERROR_NO_CONNECTION;
    }
    if (!_mxchip.getIPAddress()) {
        return NSAPI_ERROR_DHCP_FAILURE;
    }

    return NSAPI_ERROR_OK;
}

int MXCHIPInterface::set_credentials(const char *ssid, const char *pass, nsapi_security_t security)
{
    memset(ap_ssid, 0, sizeof(ap_ssid));
    strncpy(ap_ssid, ssid, sizeof(ap_ssid));

    memset(ap_pass, 0, sizeof(ap_pass));
    strncpy(ap_pass, pass, sizeof(ap_pass));

    ap_sec = security;

    return 0;
}

int MXCHIPInterface::set_channel(uint8_t channel)
{
    return _mxchip.setChannel(channel);
}

int MXCHIPInterface::disconnect()
{
    _mxchip.setTimeout(MXCHIP_MISC_TIMEOUT);

    if (!_mxchip.disconnect()) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    return 0;
}

const char* MXCHIPInterface::get_ip_address()
{
    return _mxchip.getIPAddress();
}

const char* MXCHIPInterface::get_mac_address()
{
    return _mxchip.getMACAddress();
}

const char* MXCHIPInterface::get_gateway()
{
    return NULL;
}

const char* MXCHIPInterface::get_netmask()
{
    return NULL;
}


int8_t MXCHIPInterface::get_rssi(){
    return _mxchip.getRSSI();
}


int MXCHIPInterface::scan(WiFiAccessPoint *ap, unsigned count)
{
    return NSAPI_ERROR_UNSUPPORTED;
}


struct MXCHIP_socket {
    int id;
    int socketId;
    nsapi_protocol_t proto;
    bool connected;
};

int MXCHIPInterface::socket_open(void **handle, nsapi_protocol_t proto)
{
    // Look for an unused socket
    int id = -1;

    for (int i = 1; i < MXCHIP_SOCKET_COUNT; i++) {
        if (!_ids[i]) {
            id = i;
            _ids[i] = true;
            break;
        }
    }

    if (id == -1) {
        return NSAPI_ERROR_NO_SOCKET;
    }

    struct MXCHIP_socket *socket = new struct MXCHIP_socket;
    if (!socket) {
        return NSAPI_ERROR_NO_SOCKET;
    }

    socket->id = id;
    socket->socketId = 0;
    socket->proto = proto;
    socket->connected = false;
    *handle = socket;
    return 0;
}

int MXCHIPInterface::socket_close(void *handle)
{
    struct MXCHIP_socket *socket = (struct MXCHIP_socket *)handle;
    int err = 0;
    _mxchip.setTimeout(MXCHIP_MISC_TIMEOUT);

    if (socket->connected && !_mxchip.close(socket->socketId)) {
        err = NSAPI_ERROR_DEVICE_ERROR;
    }

    socket->connected = false;
    _ids[socket->id] = false;
    delete socket;
    return err;
}

int MXCHIPInterface::socket_bind(void *handle, const SocketAddress &address)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int MXCHIPInterface::socket_listen(void *handle, int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int MXCHIPInterface::socket_connect(void *handle, const SocketAddress &addr)
{
    struct MXCHIP_socket *socket = (struct MXCHIP_socket *)handle;
    _mxchip.setTimeout(MXCHIP_MISC_TIMEOUT);
    const char *proto = (socket->proto == NSAPI_TCP) ?  "CLIENT":"UNICAST";
    socket -> socketId = _mxchip.open(proto, socket->id, addr.get_ip_address(), addr.get_port());
    if (!(socket -> socketId)) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    socket->connected = true;

    return 0;
}

int MXCHIPInterface::socket_accept(void *server, void **handle, SocketAddress *addr )
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int MXCHIPInterface::socket_send(void *handle, const void *data, unsigned size)
{
    struct MXCHIP_socket *socket = (struct MXCHIP_socket *)handle;
    _mxchip.setTimeout(MXCHIP_SEND_TIMEOUT);

    if (!_mxchip.send(socket->socketId, data, size)) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    return size;
}

int MXCHIPInterface::socket_recv(void *handle, void *data, unsigned size)
{
    struct MXCHIP_socket *socket = (struct MXCHIP_socket *)handle;
    _mxchip.setTimeout(MXCHIP_RECV_TIMEOUT);

    int32_t recv = _mxchip.recv(socket->socketId, data, size);
    if (recv < 0) {
        return NSAPI_ERROR_WOULD_BLOCK;
    }

    return recv;
}

int MXCHIPInterface::socket_sendto(void *handle, const SocketAddress &addr, const void *data, unsigned size)
{
    struct MXCHIP_socket *socket = (struct MXCHIP_socket *)handle;
    if (!socket->connected) {
        int err = socket_connect(socket, addr);
        if (err < 0) {
            return err;
        }
    }

    return socket_send(socket, data, size);
}

int MXCHIPInterface::socket_recvfrom(void *handle, SocketAddress *addr, void *data, unsigned size)
{
    struct MXCHIP_socket *socket = (struct MXCHIP_socket *)handle;
    return socket_recv(socket, data, size);
}

void MXCHIPInterface::socket_attach(void *handle, void (*callback)(void *), void *data)
{
    struct MXCHIP_socket *socket = (struct MXCHIP_socket *)handle;
    _cbs[socket->id].callback = callback;
    _cbs[socket->id].data = data;
}

void MXCHIPInterface::event() {
    for (int i = 0; i < MXCHIP_SOCKET_COUNT; i++) {
        if (_cbs[i].callback) {
            _cbs[i].callback(_cbs[i].data);
        }
    }
}
