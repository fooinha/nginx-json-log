#ifndef __NGX_KASHA_KAFKA_H__
#define __NGX_KASHA_KAFKA_H__

#include <ngx_core.h>
#include <librdkafka/rdkafka.h>

/* configuration */
rd_kafka_conf_t *
http_log_json_kafka_conf_new(ngx_pool_t *pool);

rd_kafka_conf_res_t
http_log_json_kafka_conf_set_int(ngx_pool_t *pool,rd_kafka_conf_t *conf,
                         const char * key, intmax_t value);

rd_kafka_conf_res_t
http_log_json_kafka_conf_set_str(ngx_pool_t *pool, rd_kafka_conf_t *conf,
                         const char * key, ngx_str_t *str);

/* handler */
rd_kafka_t *
http_log_json_kafka_producer_new(ngx_pool_t *pool, rd_kafka_conf_t *);

/* tries to add a comma separated list of brokers */
size_t
http_log_json_kafka_add_brokers(ngx_pool_t *pool, rd_kafka_t *rk, ngx_array_t *brokers);

/* topic confifuration */
rd_kafka_topic_conf_t *
http_log_json_kafka_topic_conf_new(ngx_pool_t* pool);

/* topic */
rd_kafka_topic_t *
http_log_json_kafka_topic_new(ngx_pool_t *pool, rd_kafka_t *rk,
                      rd_kafka_topic_conf_t *topic_conf, ngx_str_t *topic);

rd_kafka_conf_res_t
http_log_json_kafka_topic_conf_set_str(ngx_pool_t *pool, rd_kafka_topic_conf_t *topic_conf,
                               const char *key, ngx_str_t *str);

#endif// __NGX_KASHA_KAFKA_H__
