// ----------------------------------------------------------------------------
// Copyright 2016-2019 ARM Ltd.
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------

#include "MQTTAsync.h"
#include "MQTTClientPersistence.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include "simplem2mclient.h"

volatile int finished = 0;
int subscribed = 0;
int disconnected = 0;

char *topic     = "popup-iot/12/1/11/0";
char *clientid  = "paho-c-sub";
int qos = 0;
// hyhwang
int keepalive = 10; //10;
int MQTTVersion = MQTTVERSION_DEFAULT;

pthread_t t_mqtt, t_dweet;

int32_t mystamp = 0;

CURL *curl;
CURLcode res;

// Pointers to the resources that will be created in main_application().
static M2MResource* button_res;
static M2MResource* mystamp_res;
static M2MResource* pattern_res;
static M2MResource* blink_res;
static M2MResource* unregister_res;
static M2MResource* factory_reset_res;

void unregister(void);

void postDweet(char *name, int value)
{
  char data[BUFSIZ] = "";

  curl = curl_easy_init();

  if(curl) {
    sprintf(data, "{\"%s\": %d}", name, value);
    printf("> %s \n", data);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "charsets: utf-8");

    curl_easy_setopt(curl, CURLOPT_URL, "https://dweet.io:443/dweet/for/popup-iot");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
  }
}


int messageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    printf("%d %s\t", message->payloadlen, topicName);
    printf("%.*s\n", message->payloadlen, (char*)message->payload);
    char buf[BUFSIZ] = "";
    sprintf(buf, "%.*s\n", message->payloadlen, (char*)message->payload);
    mystamp = atoi(buf);
    fflush(stdout);

    //postDweet("temperature", temperature);
    //temperature_res->set_value(temperature);

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}


void onDisconnect(void* context, MQTTAsync_successData* response)
{
    disconnected = 1;
}

void onSubscribe(void* context, MQTTAsync_successData* response)
{
    subscribed = 1;
}

void onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
    fprintf(stderr, "Subscribe failed, rc %s\n", MQTTAsync_strerror(response->code));
    finished = 1;
}


void onConnectFailure(void* context, MQTTAsync_failureData* response)
{
    fprintf(stderr, "Connect failed, rc %s\n", response ? MQTTAsync_strerror(response->code) : "none");
    finished = 1;
}

void onConnect(void* context, MQTTAsync_successData* response)
{
    MQTTAsync client = (MQTTAsync)context;
    MQTTAsync_responseOptions ropts = MQTTAsync_responseOptions_initializer;
    int rc;

printf("%s \n",__FUNCTION__);
    printf("Subscribing to topic %s with client %s at QoS %d\n", topic, clientid, qos);

    ropts.onSuccess = onSubscribe;
    ropts.onFailure = onSubscribeFailure;
    ropts.context = client;
    if ((rc = MQTTAsync_subscribe(client, topic, qos, &ropts)) != MQTTASYNC_SUCCESS)
    {
        fprintf(stderr, "Failed to start subscribe, return code %s\n", MQTTAsync_strerror(rc));
        finished = 1;
    }
}

MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;

static void*
dweet_task(void* args)
{
  while(1) {
    postDweet("mystamp",(int)mystamp);
    sleep(1);
  }
}


static void*
mqtt_task(void* args)
{
    MQTTAsync client;
    MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
    MQTTAsync_createOptions create_opts = MQTTAsync_createOptions_initializer;
    int rc = 0;

    rc = MQTTAsync_createWithOptions(&client, "m16.cloudmqtt.com:12002", clientid, MQTTCLIENT_PERSISTENCE_NONE,
            NULL, &create_opts);
    if (rc != MQTTASYNC_SUCCESS)
    {
        fprintf(stderr, "Failed to create client, return code: %s\n", MQTTAsync_strerror(rc));
        exit(EXIT_FAILURE);
    }

    rc = MQTTAsync_setCallbacks(client, client, NULL, messageArrived, NULL);
    if (rc != MQTTASYNC_SUCCESS)
    {
        fprintf(stderr, "Failed to set callbacks, return code: %s\n", MQTTAsync_strerror(rc));
        exit(EXIT_FAILURE);
    }

    conn_opts.onSuccess = onConnect;
    conn_opts.onFailure = onConnectFailure;
    conn_opts.cleansession = 1;
    conn_opts.keepAliveInterval = keepalive;
    conn_opts.MQTTVersion = MQTTVersion;
    conn_opts.context = client;
    conn_opts.automaticReconnect = 1;
    conn_opts.username = "secjuiom";
    conn_opts.password = "5ig99eBdVzGc";

    if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
    {
        fprintf(stderr, "Failed to start connect, return code %s\n", MQTTAsync_strerror(rc));
        exit(EXIT_FAILURE);
    }
    while (!subscribed) usleep(100 * 1000);

    if (finished)
        goto exit;

    while (!finished) usleep(100 * 1000);

    disc_opts.onSuccess = onDisconnect;
    if ((rc = MQTTAsync_disconnect(client, &disc_opts)) != MQTTASYNC_SUCCESS)
    {
        fprintf(stderr, "Failed to start disconnect, return code: %s\n", MQTTAsync_strerror(rc));
        exit(EXIT_FAILURE);
    }
    while (!disconnected) usleep(100 * 1000);

exit:
    MQTTAsync_destroy(&client);
    exit(EXIT_SUCCESS);
}




//////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simplem2mclient.h"
#ifdef TARGET_LIKE_MBED
#include "mbed.h"
#endif
#include "application_init.h"
#include "mcc_common_button_and_led.h"
#include "blinky.h"
#ifndef MBED_CONF_MBED_CLOUD_CLIENT_DISABLE_CERTIFICATE_ENROLLMENT
#include "certificate_enrollment_user_cb.h"
#endif

#if defined(MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS) && \
 (MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS == 1) && \
 defined(MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION) && \
 (MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION == 1)
#include "nanostack-event-loop/eventOS_scheduler.h"
#endif

// event based LED blinker, controlled via pattern_resource
#ifndef MCC_MINIMAL
static Blinky blinky;
#endif

static void main_application(void);

#if defined(MBED_CLOUD_APPLICATION_NONSTANDARD_ENTRYPOINT)
extern "C"
int mbed_cloud_application_entrypoint(void)
#else
int main(void)
#endif
{
    return mcc_platform_run_program(main_application);
}

void unregister(void);

// Pointer to mbedClient, used for calling close function.
static SimpleM2MClient *client;

void pattern_updated(const char *)
{
    printf("PUT received, new value: %s\n", pattern_res->get_value_string().c_str());
}

void blink_callback(void *)
{
    String pattern_string = pattern_res->get_value_string();    
    printf("POST executed\n");

    // The pattern is something like 500:200:500, so parse that.
    // LED blinking is done while parsing.
#ifndef MCC_MINIMAL
    const bool restart_pattern = false;
    if (blinky.start((char*)pattern_res->value(), pattern_res->value_length(), restart_pattern) == false) {
        printf("out of memory error\n");
    }
#endif
    blink_res->send_delayed_post_response();
}

void notification_status_callback(const M2MBase& object,
                            const M2MBase::MessageDeliveryStatus status,
                            const M2MBase::MessageType /*type*/)
{
    switch(status) {
        case M2MBase::MESSAGE_STATUS_BUILD_ERROR:
            printf("Message status callback: (%s) error when building CoAP message\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_RESEND_QUEUE_FULL:
            printf("Message status callback: (%s) CoAP resend queue full\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_SENT:
            printf("Message status callback: (%s) Message sent to server\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_DELIVERED:
            printf("Message status callback: (%s) Message delivered\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_SEND_FAILED:
            printf("Message status callback: (%s) Message sending failed\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_SUBSCRIBED:
            printf("Message status callback: (%s) subscribed\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_UNSUBSCRIBED:
            printf("Message status callback: (%s) subscription removed\n", object.uri_path());
            break;
        case M2MBase::MESSAGE_STATUS_REJECTED:
            printf("Message status callback: (%s) server has rejected the message\n", object.uri_path());
            break;
        default:
            break;
    }
}

void sent_callback(const M2MBase& base,
                   const M2MBase::MessageDeliveryStatus status,
                   const M2MBase::MessageType /*type*/)
{
    switch(status) {
        case M2MBase::MESSAGE_STATUS_DELIVERED:
            unregister();
            break;
        default:
            break;
    }
}

void unregister_triggered(void)
{
    printf("Unregister resource triggered\n");
    unregister_res->send_delayed_post_response();
}

void factory_reset_triggered(void*)
{
    printf("Factory reset resource triggered\n");

    // First send response, so server won't be left waiting.
    // Factory reset resource is by default expecting explicit
    // delayed response sending.
    factory_reset_res->send_delayed_post_response();

    // Then run potentially long-taking factory reset routines.
    kcm_factory_reset();
}

// This function is called when a POST request is received for resource 5000/0/1.
void unregister(void)
{
    printf("Unregister resource executed\n");
    client->close();
}

void main_application(void)
{
#if defined(__linux__) && (MBED_CONF_MBED_TRACE_ENABLE == 0)
        // make sure the line buffering is on as non-trace builds do
        // not produce enough output to fill the buffer
        setlinebuf(stdout);
#endif

    // Initialize trace-library first
    if (application_init_mbed_trace() != 0) {
        printf("Failed initializing mbed trace\n" );
        return;
    }

    // Initialize storage
    if (mcc_platform_storage_init() != 0) {
        printf("Failed to initialize storage\n" );
        return;
    }

    // Initialize platform-specific components
    if(mcc_platform_init() != 0) {
        printf("ERROR - platform_init() failed!\n");
        return;
    }

    // Print some statistics of the object sizes and their heap memory consumption.
    // NOTE: This *must* be done before creating MbedCloudClient, as the statistic calculation
    // creates and deletes M2MSecurity and M2MDevice singleton objects, which are also used by
    // the MbedCloudClient.
#ifdef MBED_HEAP_STATS_ENABLED
    print_m2mobject_stats();
#endif

    // SimpleClient is used for registering and unregistering resources to a server.
    SimpleM2MClient mbedClient;

    // Save pointer to mbedClient so that other functions can access it.
    client = &mbedClient;

    /*
     * Pre-initialize network stack and client library.
     *
     * Specifically for nanostack mesh networks on Mbed OS platform it is important to initialize
     * the components in correct order to avoid out-of-memory issues in Device Management Client initialization.
     * The order for these use cases should be:
     * 1. Initialize network stack using `nsapi_create_stack()` (Mbed OS only). // Implemented in `mcc_platform_interface_init()`.
     * 2. Initialize Device Management Client using `init()`.                   // Implemented in `mbedClient.init()`.
     * 3. Connect to network interface using 'connect()`.                       // Implemented in `mcc_platform_interface_connect()`.
     * 4. Connect Device Management Client to service using `setup()`.          // Implemented in `mbedClient.register_and_connect)`.
     */
    (void) mcc_platform_interface_init();
    mbedClient.init();

    // application_init() runs the following initializations:
    //  1. platform initialization
    //  2. print memory statistics if MBED_HEAP_STATS_ENABLED is defined
    //  3. FCC initialization.
    if (!application_init()) {
        printf("Initialization failed, exiting application!\n");
        return;
    }

    // Print platform information
    mcc_platform_sw_build_info();

    // Initialize network
    if (!mcc_platform_interface_connect()) {
        printf("Network initialized, registering...\n");
    } else {
        return;
    }

#ifdef MBED_HEAP_STATS_ENABLED
    printf("Client initialized\r\n");
    print_heap_stats();
#endif
#ifdef MBED_STACK_STATS_ENABLED
    print_stack_statistics();
#endif

#ifndef MCC_MEMORY
    // Create resource for button count. Path of this resource will be: 3200/0/5501.
    button_res = mbedClient.add_cloud_resource(3200, 0, 5501, "button_resource", M2MResourceInstance::INTEGER,
                              M2MBase::GET_ALLOWED, 0, true, NULL, (void*)notification_status_callback);
    button_res->set_value(0);

    mystamp_res = mbedClient.add_cloud_resource(2936, 0, 8514, "mystamp_resource", M2MResourceInstance::INTEGER,
                              M2MBase::GET_ALLOWED, 0, true, NULL, (void*)notification_status_callback);
    mystamp_res->set_value(0);

    // Create resource for led blinking pattern. Path of this resource will be: 3201/0/5853.
    pattern_res = mbedClient.add_cloud_resource(3201, 0, 5853, "pattern_resource", M2MResourceInstance::STRING,
                               M2MBase::GET_PUT_ALLOWED, "500:500:500:500", true, (void*)pattern_updated, (void*)notification_status_callback);

    // Create resource for starting the led blinking. Path of this resource will be: 3201/0/5850.
    blink_res = mbedClient.add_cloud_resource(3201, 0, 5850, "blink_resource", M2MResourceInstance::STRING,
                             M2MBase::POST_ALLOWED, "", false, (void*)blink_callback, (void*)notification_status_callback);
    // Use delayed response
    blink_res->set_delayed_response(true);

    // Create resource for unregistering the device. Path of this resource will be: 5000/0/1.
    unregister_res = mbedClient.add_cloud_resource(5000, 0, 1, "unregister", M2MResourceInstance::STRING,
                 M2MBase::POST_ALLOWED, NULL, false, (void*)unregister_triggered, (void*)sent_callback);
    unregister_res->set_delayed_response(true);

    // Create optional Device resource for running factory reset for the device. Path of this resource will be: 3/0/6.
    factory_reset_res = M2MInterfaceFactory::create_device()->create_resource(M2MDevice::FactoryReset);
    if (factory_reset_res) {
        factory_reset_res->set_execute_function(factory_reset_triggered);
    }

#endif

    printf("%s \n", topic);

    curl = curl_easy_init();
    pthread_create(&t_mqtt,  NULL, mqtt_task,  NULL);
    pthread_create(&t_dweet, NULL, dweet_task, NULL);

// For high-latency networks with limited total bandwidth combined with large number
// of endpoints, it helps to stabilize the network when Device Management Client has
// delayed registration to Device Management after the network formation.
// This is applicable in large Wi-SUN networks.
#if defined(STARTUP_MAX_RANDOM_DELAY) && (STARTUP_MAX_RANDOM_DELAY > 0)
    wait_application_startup_delay();
#endif

    mbedClient.register_and_connect();

#ifndef MCC_MINIMAL
    blinky.init(mbedClient, mystamp_res);
    blinky.request_next_loop_event();
    blinky.request_automatic_increment_event();
#endif


#ifndef MBED_CONF_MBED_CLOUD_CLIENT_DISABLE_CERTIFICATE_ENROLLMENT
    // Add certificate renewal callback
    mbedClient.get_cloud_client().on_certificate_renewal(certificate_renewal_cb);
#endif // MBED_CONF_MBED_CLOUD_CLIENT_DISABLE_CERTIFICATE_ENROLLMENT

#if defined(MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS) && \
 (MBED_CONF_NANOSTACK_HAL_EVENT_LOOP_USE_MBED_EVENTS == 1) && \
 defined(MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION) && \
 (MBED_CONF_EVENTS_SHARED_DISPATCH_FROM_APPLICATION == 1)
    printf("Starting mbed eventloop...\n");

    eventOS_scheduler_mutex_wait();

    EventQueue *queue = mbed::mbed_event_queue();
    queue->dispatch_forever();
#else

    // Check if client is registering or registered, if true sleep and repeat.
    while (mbedClient.is_register_called()) {
        mcc_platform_do_wait(100);
    }

    // Client unregistered, disconnect and exit program.
    mcc_platform_interface_close();
#endif
}
