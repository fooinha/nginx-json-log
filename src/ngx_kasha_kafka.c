#include <ngx_kasha_kafka.h>
#include <ngx_kasha_str.h>

/* kafka configuration */
rd_kafka_topic_conf_t *
kasha_kafka_topic_conf_new(ngx_pool_t *pool) {
    rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
    if (!topic_conf) {
        ngx_log_error(NGX_LOG_CRIT, pool->log, 0,
                "kasha: Error allocating kafka topic conf");
    }
    return topic_conf;
}

/* create new configuration */
rd_kafka_conf_t *
kasha_kafka_conf_new(ngx_pool_t *pool) {
    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    if (!conf) {
        ngx_log_error(NGX_LOG_CRIT, pool->log, 0,
                "kasha: Error allocating kafka conf");
    }
    return conf;
}

/* set an integer config value */
rd_kafka_conf_res_t
kasha_kafka_conf_set_int(ngx_pool_t *pool, rd_kafka_conf_t *conf, const char * key, intmax_t value) {

    char buf[21] = {0};
    uint32_t sz  = sizeof(buf);
    char errstr[2048]  = {0};
    uint32_t errstr_sz = sizeof(errstr);

    uint32_t offset = 0;
    snprintf(buf, 21, "%lu", value);

    rd_kafka_conf_res_t ret = rd_kafka_conf_set(conf, key, buf, errstr, errstr_sz);
    if (ret != RD_KAFKA_CONF_OK) {
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "kasha: Failed to set kafka conf [%s] => [%s] : %s", key, buf, errstr);
    }
    return ret;
}

/* set a string config value */
rd_kafka_conf_res_t
kasha_kafka_conf_set_str(ngx_pool_t *pool, rd_kafka_conf_t *conf, const char * key, ngx_str_t *str) {
    char errstr[2048]  = {0};
    uint32_t errstr_sz = sizeof(errstr);

    u_char *value = ngx_kasha_str_dup(pool, str);

    rd_kafka_conf_res_t ret = rd_kafka_conf_set(conf, key, (const char *) value, errstr, errstr_sz);
    if(ret != RD_KAFKA_CONF_OK) {
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "kasha: Failed to set kafka conf [%s] => [%s] : %s", key,(const char *) value, errstr);
    }
    return ret;
}

rd_kafka_conf_res_t kasha_kafka_topic_conf_set_str(ngx_pool_t *pool, rd_kafka_topic_conf_t *topic_conf, const char *key, ngx_str_t *str) {

    char errstr[2048]  = {0};
    uint32_t errstr_sz = sizeof(errstr);

    u_char *value = ngx_kasha_str_dup(pool, str);

    rd_kafka_conf_res_t ret = rd_kafka_topic_conf_set(topic_conf, key, (const char *) value, errstr, errstr_sz);
    if(ret != RD_KAFKA_CONF_OK) {
        ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                "kasha: Failed to set kafka topic conf [%s] => [%s] : %s", key,(const char *) value, errstr);
    }
    return ret;

}

/* create a kafka handler for producing messages */
rd_kafka_t*
kasha_kafka_producer_new(ngx_pool_t *pool, rd_kafka_conf_t * conf) {

    rd_kafka_t *rk                    = NULL;
    char errstr[2048]  = {0};

    /* Create Kafka handle */
    if (!(rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr)))) {
        ngx_log_error(NGX_LOG_CRIT, pool->log, 0,
                "kasha: Error allocating kafka handler");
        return NULL;
    }

    return rk;
}

size_t kasha_kafka_add_brokers(ngx_pool_t *pool, rd_kafka_t *rk, ngx_array_t *brokers) {

    ngx_str_t    *rec;
    ngx_str_t    *broker;
	u_char       *value = NULL;

    size_t       ret = 0;

    rec = brokers->elts;
    for (size_t i = 0; i < brokers->nelts; ++i) {
		broker = &rec[i];
		value = ngx_kasha_str_dup(pool, broker);

		if ( rd_kafka_brokers_add(rk, (const char *) value)){
			ngx_log_error(NGX_LOG_INFO, pool->log, 0,
					"kasha: broker \"%V\" configured", broker);
			++ret;
		} else {
			ngx_log_error(NGX_LOG_WARN, pool->log, 0,
					"kasha: failed to configure \"%V\"", broker);
		}
    }
    return ret;
}

/* creates a configured topic */
rd_kafka_topic_t * kasha_kafka_topic_new(ngx_pool_t *pool, rd_kafka_t *rk, rd_kafka_topic_conf_t *topic_conf, ngx_str_t *topic) {

    u_char *value = ngx_kasha_str_dup(pool, topic);

	if (! rk ) {
        ngx_log_error(NGX_LOG_CRIT, pool->log, 0,
                "kasha: missing kafka handler");
		return NULL;
	}

	rd_kafka_topic_t * rkt = rd_kafka_topic_new(rk, (const char *)value, topic_conf);
	if(!rkt) {
            /* FIX ME - Why sooooo quiet! */
            ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                    "kasha: failed to create topic \"%V\"", topic);
	}

	return rkt;

}
