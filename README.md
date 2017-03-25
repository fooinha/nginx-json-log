# ngx-http-log-json  [![Build Status](https://travis-ci.org/fooinha/ngx-http-log-json.svg?branch=master)](https://travis-ci.org/fooinha/ngx-http-log-json)

nginx http module for logging in custom json format - aka kasha (🍲)

## Description

This module adds to nginx the ability of advanced JSON logging of HTTP requests per location.

It's possible to log to a destination ouput any request made to a specific nginx location.

The output format is configurable.
It also allows to log complex and multi-level JSON documents.

## Use cases

That are many use cases.

Many things can be done by using the access log data.

Having it in JSON format makes easier for integration with other platforms and applications.

A quick example:


![](docs/use-case-kafka-logs.png?raw=true)


## Use cases

That are many use cases.

Many things can be done by using the access log data.

Having it in JSON format makes easier for integration with other platforms and applications.

A quick example:


![](docs/use-case-kafka-logs.png?raw=true)


### Configuration

Each logging configuration is based on a http_log_json_format. (🍲)

A http_log_json spec is a ';' separated list of items to include in the logging preparation.

The left hand side part of item will be the JSON Path for the variable name
The left hand side part can be prefixed with 's:', 'i:' or 'r:', so the JSON encoding type can be controlled.

* 's:' - JSON string ( default )
* 'i:' - JSON integer
* 'r:' - JSON real
* 'b:' - JSON boolean
* 'n:' - JSON null

The right hand side will be the variable's name or literal value.
For this, known or previously setted variables, can be used by using the '$' before name.

Common HTTP nginx builtin variables like $uri, or any other variable set by other handler modules can be used.

The output is sent to the location specified by the first http_log_json_format argument.
The possible output locations are:

* "file:" - The logging location will be a local filesystem file.
* "kafka:" - The logging location will be a Kafka topic.

#### Example Configuration


##### A simple configuration example

```yaml
     http_log_json_format file:/tmp/log '
        src.ip                      $remote_addr;
        src.port                    $remote_port;
        dst.ip                      $server_addr;
        dst.port                    $server_port;
        _date                       $time_iso8601;
        r:_real                     1.1;
        i:_int                      2016;
        i:_status                   $status;
        b:_notrack                  false;
        _literal                    root;
        comm.proto                  http;
        comm.http.method            $request_method;
        comm.http.path              $uri;
        comm.http.host              $host;
        comm.http.server_name       $server_name;
     ';
```

This will produce the following JSON line to '/tmp/log' file .
To ease reading, it's shown here formatted with newlines.

```json
{
  "_date": "2016-12-11T18:06:54+00:00",
  "_int": 2016,
  "_literal": "root",
  "_real": 1.1,
  "_status": 200,
  "_notrack": false,
  "comm": {
    "http": {
      "host": "localhost",
      "method": "HEAD",
      "path": "/index.html",
      "server_name": "localhost"
    },
    "proto": "http"
  },
  "dst": {
    "ip": "127.0.0.1",
    "port": "80"
  },
  "src": {
    "ip": "127.0.0.1",
    "port": "52136"
  }
}
```

##### A example using perl handler variables.

```yaml
       perl_set $bar '
           sub {
            my $r = shift;
            my $uri = $r->uri;

            return "yes" if $uri =~ /^\/bar/;
                        return "";
        }';

       http_log_json_format file:/tmp/log '
        comm.http.server_name       $server_name;
        perl.bar                    $bar;
       ';
 ```

 A request sent to **/bar** .

 ```json
{

  "comm": {
    "http": {
      "server_name": "localhost"
    }
  },
  "perl": {
    "bar": "yes"
  }
}
```

### Directives

---
* Syntax: **http_log_json_format** _location_ { _format_ };
* Default: —
* Context: http location

###### _location_ ######

Specifies the location for the output...

The output location type should be prefixed with supported location types. ( **file:** or **kafka:** )

For a **file:** type the value part will be a local file name. e.g. **file:**/tmp/log

For a **kafka:** type the value part will be the topic name. e.g. **kafka:** topic

The kafka output only happens if a list of brokers is defined by **http_log_json_kafka_brokers** directive.

###### _format_ ######

See details above.

---

* Syntax: **"http_log_json_kafka_partition** _compression_codec_;
* Default: RD_KAFKA_PARTITION_UA
* Context: http local

---

* Syntax: **http_log_json_kafka_brokers** list of brokers separated by spaces;
* Default: —
* Context: http main

---

* Syntax: **http_log_json_kafka_client_id** _id_;
* Default: http_log_json
* Context: http main

---

* Syntax: **"http_log_json_kafka_compression** _compression_codec_;
* Default: snappy
* Context: http main

---

* Syntax: **"http_log_json_kafka_log_level** _numeric_log_level_;
* Default: 6
* Context: http main

---

* Syntax: **"http_log_json_kafka_max_retries** _numeric_;
* Default: 0
* Context: http main

---

* Syntax: **"http_log_json_kafka_buffer_max_messages** _numeric_;
* Default: 100000
* Context: http main

---

* Syntax: **"http_log_json_kafka_backoff_ms** _numeric_;
* Default: 10
* Context: http main



### Build

#### Dependencies

* [libjansson](http://www.digip.org/jansson/)
* [librdkafka](https://github.com/edenhill/librdkafka)

For Ubuntu or Debian install development packages.

```bash
$ sudo apt-get install libjansson-dev librdkafka-dev

```

Build as a common nginx module.

```bash
$ ./configure --add-module=/build/ngx-http_log_json
$ make && make install

```



### Tests and Fair Warning

**THIS IS NOT PRODUCTION** ready.

So there's no guarantee of success. It most probably blow up when running in real life scenarios.

#### Unit tests

The unit tests use https://github.com/openresty/test-nginx framework.


```
$ git clone https://github.com/openresty/test-nginx.git
$ cd test-nginx/
$ cpanm .
$ export PATH=$PATH:/usr/local/nginx/sbin/
```

At project root just run the prove command:

```
$ prove

t/0001_simple_file_log.t .. ok
All tests successful.
Files=1, Tests=1,  0 wallclock secs ( 0.02 usr  0.01 sys +  0.15 cusr  0.00 csys =  0.18 CPU)
Result: PASS

```

