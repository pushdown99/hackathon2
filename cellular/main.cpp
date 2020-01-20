/*
 * Copyright (c) 2017 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "common_functions.h"
#include "UDPSocket.h"
#include "CellularLog.h"
#include <MQTTClientMbedOs.h>

#include "HTS221Sensor.h"

static DevI2C devI2c(PB_11,PB_10);
static HTS221Sensor sen_hum_temp(&devI2c);

#define SENSORS_POLL_INTERVAL 3.0

#define UDP 0
#define TCP 1

// Number of retries /
#define RETRY_COUNT 3

NetworkInterface *iface;

// Echo server hostname
const char *host_name = MBED_CONF_APP_ECHO_SERVER_HOSTNAME;

// Echo server port (same for TCP and UDP)
const int port = MBED_CONF_APP_ECHO_SERVER_PORT;

// An event queue is a very useful structure to debounce information between contexts (e.g. ISR and normal threads)
// This is great because things such as network operations are illegal in ISR, so updating a resource in a button's fall() function is not allowed
//EventQueue eventQueue;
Thread thread;
float temp1_value, humid_value;
    
static rtos::Mutex trace_mutex;

/**
 * Initialize sensors
 */
void sensors_init() {
    uint8_t id1, id2, id3, id4;

    printf ("\nSensors configuration:\n");
    // Initialize sensors
    sen_hum_temp.init(NULL);

    /// Call sensors enable routines
    sen_hum_temp.enable();
    sen_hum_temp.read_id(&id1);

    printf("HTS221  humidity & temperature    = 0x%X\n", id1);
    printf("\n"); ;
}

void sensors_update() {
    while(1) {
        sen_hum_temp.get_humidity(&humid_value);
        sen_hum_temp.get_temperature(&temp1_value);

        printf("HTS221 temp:  %7.3f C,  humidity: %7.2f %%         \n", temp1_value, humid_value);
        //printf("\r\033[8A");
        wait(1);
    }
}

#if MBED_CONF_MBED_TRACE_ENABLE
static void trace_wait()
{
    trace_mutex.lock();
}

static void trace_release()
{
    trace_mutex.unlock();
}

static char time_st[50];

static char* trace_time(size_t ss)
{
    snprintf(time_st, 49, "[%08llums]", Kernel::get_ms_count());
    return time_st;
}

static void trace_open()
{
    mbed_trace_init();
    mbed_trace_prefix_function_set( &trace_time );

    mbed_trace_mutex_wait_function_set(trace_wait);
    mbed_trace_mutex_release_function_set(trace_release);

    mbed_cellular_trace::mutex_wait_function_set(trace_wait);
    mbed_cellular_trace::mutex_release_function_set(trace_release);
}

static void trace_close()
{
    mbed_cellular_trace::mutex_wait_function_set(NULL);
    mbed_cellular_trace::mutex_release_function_set(NULL);

    mbed_trace_free();
}
#endif // #if MBED_CONF_MBED_TRACE_ENABLE

Thread dot_thread(osPriorityNormal, 512);

void print_function(const char *format, ...)
{
    trace_mutex.lock();
    va_list arglist;
    va_start( arglist, format );
    vprintf(format, arglist);
    va_end( arglist );
    trace_mutex.unlock();
}

void dot_event()
{
    while (true) {
        ThisThread::sleep_for(4000);
        if (iface && iface->get_connection_status() == NSAPI_STATUS_GLOBAL_UP) {
            break;
        } else {
            trace_mutex.lock();
            printf(".");
            fflush(stdout);
            trace_mutex.unlock();
        }
    }
}

/**
 * Connects to the Cellular Network
 */
nsapi_error_t do_connect()
{
    nsapi_error_t retcode = NSAPI_ERROR_OK;
    uint8_t retry_counter = 0;

    while (iface->get_connection_status() != NSAPI_STATUS_GLOBAL_UP) {
        retcode = iface->connect();
        if (retcode == NSAPI_ERROR_AUTH_FAILURE) {
            print_function("\n\nAuthentication Failure. Exiting application\n");
        } else if (retcode == NSAPI_ERROR_OK) {
            print_function("\n\nConnection Established.\n");
        } else if (retry_counter > RETRY_COUNT) {
            print_function("\n\nFatal connection failure: %d\n", retcode);
        } else {
            print_function("\n\nCouldn't connect: %d, will retry\n", retcode);
            retry_counter++;
            continue;
        }
        break;
    }
    return retcode;
}

/**
 * Opens a UDP or a TCP socket with the given echo server and performs an echo
 * transaction retrieving current.
 */
nsapi_error_t test_send_recv()
{
    nsapi_size_or_error_t retcode;
#if MBED_CONF_APP_SOCK_TYPE == TCP
    TCPSocket sock;
#else
    UDPSocket sock;
#endif

    retcode = sock.open(iface);
    if (retcode != NSAPI_ERROR_OK) {
#if MBED_CONF_APP_SOCK_TYPE == TCP
        print_function("TCPSocket.open() fails, code: %d\n", retcode);
#else
        print_function("UDPSocket.open() fails, code: %d\n", retcode);
#endif
        return -1;
    }

    SocketAddress sock_addr;
    retcode = iface->gethostbyname(host_name, &sock_addr);
    if (retcode != NSAPI_ERROR_OK) {
        print_function("Couldn't resolve remote host: %s, code: %d\n", host_name, retcode);
        return -1;
    }

    sock_addr.set_port(port);

    sock.set_timeout(15000);
    int n = 0;
    const char *echo_string = "TEST";
    char recv_buf[4];
#if MBED_CONF_APP_SOCK_TYPE == TCP
    retcode = sock.connect(sock_addr);
    if (retcode < 0) {
        print_function("TCPSocket.connect() fails, code: %d\n", retcode);
        return -1;
    } else {
        print_function("TCP: connected with %s server\n", host_name);
    }
while(1) {
    retcode = sock.send((void*) echo_string, sizeof(echo_string));
    if (retcode < 0) {
        print_function("TCPSocket.send() fails, code: %d\n", retcode);
        return -1;
    } else {
        print_function("TCP: Sent %d Bytes to %s\n", retcode, host_name);
    }

    n = sock.recv((void*) recv_buf, sizeof(recv_buf));
    ThisThread::sleep_for(4000);
}
#else

while(1) {
    retcode = sock.sendto(sock_addr, (void*) echo_string, sizeof(echo_string));
    if (retcode < 0) {
        print_function("UDPSocket.sendto() fails, code: %d\n", retcode);
        return -1;
    } else {
        print_function("UDP: Sent %d Bytes to %s\n", retcode, host_name);
    }

    n = sock.recvfrom(&sock_addr, (void*) recv_buf, sizeof(recv_buf));
    ThisThread::sleep_for(4000);
}
#endif

    sock.close();

    if (n > 0) {
        print_function("Received from echo server %d Bytes\n", n);
        return 0;
    }

    return -1;
}

nsapi_error_t mqtt_send ()
{
    char hostname[32] = "222.112.107.46";
    int port = 1883;
    char *topic = "popup-iot/12/1/11/0";
    
    NetworkInterface *net = NetworkInterface::get_default_instance(); 
    TCPSocket socket;
    
    if(socket.open(net) != NSAPI_ERROR_OK) {
        print_function("[MQTT] Socket open failed. \n");
        return -1;
    } 
    print_function("[MQTT] Socket open success. \n");
    
    if(socket.connect(hostname, port) != NSAPI_ERROR_OK) {
        print_function("[MQTT] Socket connection failed (%s:%d)\n", hostname, port);
        return -1;
    }
    print_function("[MQTT] Socket connection success. (%s:%d)\n", hostname, port);
    MQTTClient client(&socket);
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    
    data.clientID.cstring = (char *)"MQTT_CONNECT";
    if(client.connect(data) != NSAPI_ERROR_OK) {
        print_function("[MQTT] Connection failed\n");
        return -1;
    }
    print_function("[MQTT] Connection success \n");
    
    
    
    while(1) {
        char buf[512];
        sprintf(buf, "%7.2f", temp1_value);
    
        MQTT::Message msg;
 
        msg.qos = MQTT::QOS0;
        msg.retained = false;
        msg.dup = false;
        msg.payload = (void *)buf;
        msg.payloadlen = strlen(buf) + 1;

        print_function("[MQTT] Publishing.... \n");
        client.publish(topic, msg);
        ThisThread::sleep_for(4000);
    }
        
    return 0;
}

int main()
{
    DigitalOut TPB23_RESET(A1);
    TPB23_RESET = 0;    /* 0: Standby 1: Reset */
    
    sensors_init();

    // The timer fires on an interrupt context, but debounces it to the eventqueue, so it's safe to do network operations
    //Ticker timer;
    //timer.attach(eventQueue.event(&sensors_update), SENSORS_POLL_INTERVAL);
    thread.start(sensors_update);
        
    print_function("\n\nmbed-os-example-cellular\n");
    print_function("\n\nBuilt: %s, %s\n", __DATE__, __TIME__);
#ifdef MBED_CONF_NSAPI_DEFAULT_CELLULAR_PLMN
    print_function("\n\n[MAIN], plmn: %s\n", (MBED_CONF_NSAPI_DEFAULT_CELLULAR_PLMN ? MBED_CONF_NSAPI_DEFAULT_CELLULAR_PLMN : "NULL"));
#endif

    print_function("Establishing connection\n");
#if MBED_CONF_MBED_TRACE_ENABLE
    trace_open();
#else
    dot_thread.start(dot_event);
#endif // #if MBED_CONF_MBED_TRACE_ENABLE

    // sim pin, apn, credentials and possible plmn are taken atuomtically from json when using get_default_instance()
    iface = NetworkInterface::get_default_instance();
    MBED_ASSERT(iface);

    nsapi_error_t retcode = NSAPI_ERROR_NO_CONNECTION;

    /* Attempt to connect to a cellular network */
    if (do_connect() == NSAPI_ERROR_OK) {
        //retcode = test_send_recv();
        retcode = mqtt_send();
    }

    if (iface->disconnect() != NSAPI_ERROR_OK) {
        print_function("\n\n disconnect failed.\n\n");
    }

    if (retcode == NSAPI_ERROR_OK) {
        print_function("\n\nSuccess. Exiting \n\n");
    } else {
        print_function("\n\nFailure. Exiting \n\n");
    }

#if MBED_CONF_MBED_TRACE_ENABLE
    trace_close();
#else
    dot_thread.terminate();
#endif // #if MBED_CONF_MBED_TRACE_ENABLE

    return 0;
}
// EOF
