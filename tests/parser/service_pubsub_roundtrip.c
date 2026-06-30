/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 *
 * Round-trip driver fixture for test_service_pubsub.c. This file is
 * NOT compiled by CMake directly: the test generates the `calc` schema
 * into a temp directory, copies this driver alongside it, then compiles
 * and links it against the generated code + the runtime and runs it.
 * It exercises the generated service and pub/sub glue through mock
 * transports (no real network).
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zserio/allocator.h"
#include "zserio/pubsub.h"
#include "zserio/service.h"

#include "calc/calculator.h"
#include "calc/request.h"
#include "calc/response.h"
#include "calc/sample.h"
#include "calc/sensor_bus.h"

/* ---- service handlers ---- */
static zserio_status_t h_add(void *ctx, const calc_request_t *req, calc_response_t *rsp,
                             const zserio_allocator_t *alloc)
{
    (void)ctx;
    (void)alloc;
    rsp->result = req->a + req->b;
    return ZSERIO_OK;
}

static zserio_status_t h_multiply(void *ctx, const calc_request_t *req, calc_response_t *rsp,
                                  const zserio_allocator_t *alloc)
{
    (void)ctx;
    (void)alloc;
    rsp->result = req->a * req->b;
    return ZSERIO_OK;
}

/* ---- mock client transport: loop request bytes into the generated server ---- */
static zserio_service_t g_server;

static zserio_status_t loopback_call(void *impl, const char *name, size_t name_len,
                                     const uint8_t *req, size_t req_size,
                                     const zserio_allocator_t *alloc,
                                     uint8_t **rsp, size_t *rsp_size)
{
    (void)impl;
    return g_server.call_method(g_server.impl, name, name_len, req, req_size, alloc, rsp, rsp_size);
}

/* ---- mock pub/sub broker: one subscription, immediate delivery on publish ---- */
typedef struct {
    char                     topic[64];
    size_t                   topic_len;
    zserio_pubsub_on_topic_t cb;
    void                    *cbctx;
    int                      active;
} broker_t;

static zserio_status_t br_publish(void *impl, const char *topic, size_t topic_len,
                                  const uint8_t *data, size_t data_size, void *ctx)
{
    broker_t *b = (broker_t *)impl;
    (void)ctx;
    if (b->active && (b->topic_len == topic_len) && (memcmp(b->topic, topic, topic_len) == 0)) {
        b->cb(b->cbctx, topic, topic_len, data, data_size);
    }
    return ZSERIO_OK;
}

static zserio_status_t br_subscribe(void *impl, const char *topic, size_t topic_len,
                                    zserio_pubsub_on_topic_t cb, void *cbctx, void *ctx,
                                    zserio_pubsub_subscription_id_t *id)
{
    broker_t *b = (broker_t *)impl;
    (void)ctx;
    memcpy(b->topic, topic, topic_len);
    b->topic_len = topic_len;
    b->cb = cb;
    b->cbctx = cbctx;
    b->active = 1;
    *id = 1U;
    return ZSERIO_OK;
}

static zserio_status_t br_unsubscribe(void *impl, zserio_pubsub_subscription_id_t id)
{
    (void)id;
    ((broker_t *)impl)->active = 0;
    return ZSERIO_OK;
}

static int      g_delivered = 0;
static uint32_t g_dsid = 0U;
static uint32_t g_dval = 0U;

static void on_sample(void *ctx, const calc_sample_t *m)
{
    (void)ctx;
    g_delivered++;
    g_dsid = m->sensor_id;
    g_dval = m->value;
}

int main(void)
{
    const zserio_allocator_t alloc = zserio_allocator_libc();

    /* --- service round-trip --- */
    calc_calculator_handlers_t handlers = {0};
    handlers.add = h_add;
    handlers.multiply = h_multiply;
    calc_calculator_service_init(&g_server, &handlers);
    zserio_service_t transport = {loopback_call, NULL};

    calc_request_t req;
    calc_request_init(&req);
    req.a = 6U;
    req.b = 7U;

    calc_response_t r1;
    calc_response_t r2;
    assert(calc_calculator_add(&transport, &req, &r1, &alloc) == ZSERIO_OK);
    assert(r1.result == 13U);
    calc_response_free(&r1, &alloc);
    assert(calc_calculator_multiply(&transport, &req, &r2, &alloc) == ZSERIO_OK);
    assert(r2.result == 42U);
    calc_response_free(&r2, &alloc);
    assert(strcmp(calc_calculator_service_full_name(), "calc.Calculator") == 0);

    /* unknown method through the raw seam */
    uint8_t *resp = NULL;
    size_t   resp_sz = 0U;
    assert(g_server.call_method(g_server.impl, "nope", 4U, NULL, 0U, &alloc, &resp, &resp_sz)
           == ZSERIO_ERR_INVALID_ARGUMENT);
    assert((resp == NULL) && (resp_sz == 0U));

    /* --- pub/sub round-trip --- */
    broker_t broker = {0};
    zserio_pubsub_t bus = {br_publish, br_subscribe, br_unsubscribe, &broker};
    calc_sensor_bus_commanded_subscription_t sub = {0};
    sub.alloc = &alloc;
    sub.on_message = on_sample;
    zserio_pubsub_subscription_id_t id;
    assert(calc_sensor_bus_subscribe_commanded(&bus, &sub, NULL, &id) == ZSERIO_OK);

    calc_sample_t s;
    calc_sample_init(&s);
    s.sensor_id = 99U;
    s.value = 123456789U;
    assert(calc_sensor_bus_publish_published(&bus, &s, &alloc, NULL) == ZSERIO_OK);
    assert((g_delivered == 1) && (g_dsid == 99U) && (g_dval == 123456789U));
    assert(calc_sensor_bus_unsubscribe_commanded(&bus, id) == ZSERIO_OK);

    printf("ALL ROUND-TRIP ASSERTS PASSED\n");
    return 0;
}
