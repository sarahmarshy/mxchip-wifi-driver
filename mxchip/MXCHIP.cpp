/* MXCHIP Example
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

#include "../../mxchip-wifi-driver/mxchip/MXCHIP.h"

MXCHIP::MXCHIP(PinName tx, PinName rx, bool debug)
    : _serial(tx, rx, 1024), _parser(_serial)
    , _packets(0), _packets_end(&_packets)
{
    _serial.baud(115200);
    _parser.debugOn(debug);
}

bool MXCHIP::startup(void)
{
	bool success=false;
	    //Query FMODE;
		char buffer[20];
		char buffer2[20];
		memset(buffer,0,sizeof(buffer));

		for(int i=0;i<3;i++){
			_parser.send("AT+REBOOT");
			_parser.setTimeout(1000);
			_parser.recv("%[^#]#",buffer);
			_parser.setTimeout(8000);

			if(strstr(buffer,"+OK"))
				break;
			else{
				if(!(i==2)){
					//continue to send reboot command.
					continue;
				}else{
						printf("This is the first time to setup,maybe it will takes a lot of time!\r\n");
						if(_parser.write("+++",3)&& _parser.recv("a")){
							if(_parser.write("a",1)&&_parser.recv("+OK")){
								_parser.oob("+EVENT=SOCKET", this, &MXCHIP::_packet_handler);
								success= _parser.send("AT+FMODE=AT_NONE")&&
											_parser.recv("+OK")&&
											_parser.send("AT+FEVENT=ON")&&
											_parser.recv("+OK")&&
											_parser.send("AT+FACTORY")&&
											_parser.recv("+OK");
								wait(3);
								_parser.oob("+EVENT=SOCKET", this, &MXCHIP::_packet_handler);
								return success;
							}else
							{
								printf("test error!\r\n");
								  return false;
							}
						}else{
							printf("send a can't get +OK\r\n");
							return false;
						}
				}
			}
		}

    _parser.oob("+EVENT=SOCKET", this, &MXCHIP::_packet_handler);

    //Waiting for wifi module to restart
	if(!handleEvent(1,buffer2,5))
		return false;

    return true;
}

bool MXCHIP::reset(const char *reset)
{
    for (int i = 0; i < 2; i++) {
        if (_parser.send("AT+%s", reset)
            && _parser.recv("+OK")) {
            return true;
        }
    }

    return false;
}

bool MXCHIP::dhcp(bool enabled)
{
    return _parser.send("AT+DHCP=%s", enabled ? "ON":"OFF")
        && _parser.recv("+OK");
}

bool MXCHIP::connect(const char *ap, const char *passPhrase)
{
	char buffer[50];
	bool success=false;
    success= _parser.send("AT+WSTA=%s,%s", ap, passPhrase)
			&& _parser.recv("+OK");
    if(!success)
    	return false;

	_parser.send("AT+WLANF");
	_parser.recv("%*[^,],%*[^,],%*[^,],%[^#]#",buffer);
    if(strstr(buffer,"ON")){
    	if(!_parser.send("AT+WLANF=STA,OFF")&&_parser.recv("+OK")){
    		return 0;
    	}
    	if(!_parser.send("AT+WLANF=STA,ON")&&_parser.recv("+OK"))
    		return 0;
    }else
    	if(strstr(buffer,"OFF")){
			if(!_parser.send("AT+WLANF=STA,ON")&&_parser.recv("+OK")){
				return false;
			}
        }else
        	return false;
	if(!handleEvent(0,buffer,30))
		return false;
	return true;
}

bool MXCHIP::setChannel(uint8_t channel)
{
	return _parser.send("AT+WAPCH=%d",channel)
		&& _parser.recv("+OK");
}

bool MXCHIP::disconnect(void)
{
    return _parser.send("AT+WLANF=STA,OFF")
        && _parser.recv("+EVENT=WIFI_LINK,STATION_DOWN")
        && _parser.recv("+OK");
}

const char *MXCHIP::getIPAddress(void)
{
    if (!(_parser.send("AT+IPCONFIG")
        && _parser.recv("%*[^,],%*[^,],%*[^,],%[^,]%*[^#]#", _ip_buffer))) {
        return NULL;
    }
    return _ip_buffer;
}

const char *MXCHIP::getMACAddress(void)
{
    if (!(_parser.send("AT+WMAC")
        && _parser.recv("%*[^=]=%[^#]#", _mac_buffer))) {
        return 0;
    }
    return _mac_buffer;
}


//get current signal strength £»
int8_t MXCHIP::getRSSI(){
	if (!(_parser.send("AT+WLINK")
			&&_parser.recv("%*[^=]=%*[^,],%[^,],%[^#]#",_rssi_buffer)))
	    return false;
	return  atoi(_rssi_buffer);
}


bool MXCHIP::isConnected(void)
{
    return getIPAddress() != 0;
}

int MXCHIP::open(const char *type, int id, const char* addr, int port)
{
	char conType[30];       // UDP/TCP connection type: SERVER, CLIENT, BROADCAST, UNICAST
	char socketId[10];      // UDP/TCP socket id
	char buffer[50];
    int state1 = 0;
    state1 = _parser.send("AT+CON1=%s,%d,%d,%s", type, 20001, port, addr)
        && _parser.recv("+OK");

    _parser.flush();
    _parser.send("AT+CONF=1");
    _parser.recv("%[^#]#",buffer);
    if(strstr(buffer,"ON")){
    	if(!_parser.send("AT+CONF=1,OFF")&&_parser.recv("+OK")){
    		return 0;
    	}
    	if(!_parser.send("AT+CONF=1,ON")&&_parser.recv("+OK"))
    		return 0;
    }else
    	if(strstr(buffer,"OFF")){
			if(!_parser.send("AT+CONF=1,ON")&&_parser.recv("+OK")){
				return 0;
			}
        }else
        	return 0;

    handleEvent(0,buffer,40);
    sscanf(buffer,"%[^,],%*[^,],%s",conType,socketId);
	if(state1)
		return atoi(socketId);
	else
		return 0;
}

bool MXCHIP::send(int id, const void *data, uint32_t amount)
{
    //May take a second try if device is busy
    for (unsigned i = 0; i < 2; i++) {
        if (_parser.send("AT+SSEND=%d,%d", id, amount)
            && _parser.recv(">")
            && _parser.write((char*)data, (int)amount) >= 0
            && _parser.recv("+OK")) {
            //wait(3);
            return true;
        }
    }

    return false;
}

void MXCHIP::_packet_handler()
{
    int id;
    uint32_t amount;

    // parse out the packet
    if (!_parser.recv(",%d,%d,", &id, &amount)) {
        return;
    }

    struct packet *packet = (struct packet*)malloc(
            sizeof(struct packet) + amount);
    if (!packet) {
        return;
    }

    packet->id = id;
    packet->len = amount;
    packet->next = 0;

    if (!(_parser.read((char*)(packet + 1), amount))) {
        free(packet);
        return;
    }

    // append to packet list
    *_packets_end = packet;
    _packets_end = &packet->next;
}

int32_t MXCHIP::recv(int id, void *data, uint32_t amount)
{
    while (true) {
        // check if any packets are ready for us
        for (struct packet **p = &_packets; *p; p = &(*p)->next) {
            if ((*p)->id == id) {
                struct packet *q = *p;

                if (q->len <= amount) { // Return and remove full packet
                    memcpy(data, q+1, q->len);

                    if (_packets_end == &(*p)->next) {
                        _packets_end = p;
                    }
                    *p = (*p)->next;

                    uint32_t len = q->len;
                    free(q);
                    return len;
                } else { // return only partial packet
                    memcpy(data, q+1, amount);

                    q->len -= amount;
                    memmove(q+1, (uint8_t*)(q+1) + amount, q->len);

                    return amount;
                }
            }
        }

        // Wait for inbound packet
        if (!_parser.recv("#")) {
            return -1;
        }
    }
}

bool MXCHIP::close(int id)
{
    //May take a second try if device is busy
    for (unsigned i = 0; i < 2; i++) {
        if (_parser.send("AT+CONF=1,OFF")
            && _parser.recv("+OK")) {
            char conType[10];  // Used for UDP/TCP connection type: SERVER, CLIENT, BROADCAST, UNICAST
            int socketId;      // Used for UDP/TCP socket id

            _parser.recv("+EVENT=%[^,],DISCONNECT,%d", conType, &socketId);
            return true;
        }
    }

    return false;
}


void MXCHIP::setTimeout(uint32_t timeout_ms)
{
    _parser.setTimeout(timeout_ms);
}

bool MXCHIP::readable()
{
    return _serial.readable();
}

bool MXCHIP::writeable()
{
    return _serial.writeable();
}

void MXCHIP::attach(Callback<void()> func)
{
    _serial.attach(func);
}

bool MXCHIP::handleEvent(int type,char *buffer,int time){

	char a;
	Timer t;
	t.reset();

	switch(type){
	case 0:
		memset(buffer,0,sizeof(buffer));
		t.start();
	    while(true){
			if(t.read()>time){
				t.stop();
				return false;
			}
	    	// Judge EVENT
			if((a=_parser.getc())=='+'){
    			_parser.recv("%[^#]#",buffer);
				if(strstr(buffer,"UP")&&strstr(buffer,"EVENT")){
					printf("Get mode_up event!\r\n");
					t.stop();
					_parser.flush();
					return true;
				}else
				{
					if(strstr(buffer,"DISCONNECT")&&strstr(buffer,"EVENT")){
						memset(buffer,0,sizeof(buffer));
						continue;
					}else{
						if(strstr(buffer,"CONNECT")&&strstr(buffer,"EVENT")){
							t.stop();
							_parser.flush();
							return true;
						}else{
							if(strstr(buffer,"DOWN")&&strstr(buffer,"EVENT")){
								memset(buffer,0,sizeof(buffer));
								continue;
							}else{
								if(strstr(buffer,"OK")){
									memset(buffer,0,sizeof(buffer));
									continue;
								}else{
									t.stop();
									return false;
								}
							}
						}
					}
				}
			}
		}
		break;
	case 1:
		memset(buffer,0,sizeof(buffer));
		t.start();
	    while(true){
			if(t.read()>time){
				t.stop();
				return false;
			}
	    	// Judge EVENT.
			if((a=_parser.getc())=='='){
    			_parser.recv("%[^#]#",buffer);
    			if(strstr(buffer,"READY")){
    				t.stop();
    				return true;
    				break;
    			}
			}
	    }
		break;
	default:
		printf("handleEvent error!\r\n");
		return false;
		break;
	}
	return false;
}
