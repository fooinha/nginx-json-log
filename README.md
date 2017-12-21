# ngx-json-log  [![Build Status](https://travis-ci.org/fooinha/nginx-json-log.svg?branch=master)](https://travis-ci.org/fooinha/nginx-json-log)

nginx modules for logging in custom json format - aka kasha (🍲)

## Description

This module adds to nginx the ability of advanced JSON logging of HTTP requests per location.

This module adds to nginx the ability of advanced JSON logging of Stream connections per server.

It's possible to log to a destination ouput any request/session made to a specific nginx location.

The output format is configurable.

It also allows to log complex and multi-level JSON documents.

It supports logging to text file, to syslog or to a kafka topic.

It supports multiple output destinations with multiple formats for a location.

### Current version and limitations

Current version released is 0.0.7.

Stream logging is only available when using nginx (>= 1.11.2).


## Use cases

That are many use cases.

Many things can be done by using the access log data.

Having it in JSON format makes easier for integration with other platforms and applications.

A quick example:

![](docs/use-case-kafka-logs.png?raw=true)


### Configuration

Each logging configuration is based on a json_log_format. (🍲)

A json_log_format is a ';' separated list of items to include in the logging preparation.

The left hand side part of item will be the JSON Path for the variable name

The left hand side part can be prefixed with 's:', 'i:', 'r:', 'b:' or 'n:', so, the JSON encoding type can be controlled.

* 's:' - JSON string ( default )
* 'i:' - JSON integer
* 'r:' - JSON real
* 'b:' - JSON boolean
* 'n:' - JSON null

Additional prefix:

* 'a:' - JSON Array - MUST be used before other prefixes. All keys with same name and defined as array will be its values grouped together in an array. ( see example below )


The right hand side will be the variable's name or literal value.
For this, known or previously setted variables, can be used by using the '$' before name.

Common HTTP nginx builtin variables like $uri, or any other variable set by other handler modules can be used.

Common STREAM nginx builtin variables like $remote_addr, or any other variable set by other handler modules can be used.

Additional variables are provided by this module. See the available variables below at [Variables section](#variables).

The output is sent to the location specified by the first json_log_format argument.
The possible output locations are:

* "file:" - The logging location will be a local filesystem file.
* "kafka:" - The logging location will be a Kafka topic.

#### Kafka Message Id

For HTTP logging, if kafka output is used the value from $request_id nginx variable will be used to set kafka's message id.

The $request_id is only available for nginx (>=1.11.0).


#### Log HTTP Bad Requests

Nginx will short circuit response if the client sends a Bad Request. In that case, the log handler will not be run.

In order to, capture and log these requests we can define at server level, a format and output location for this requests.

The directives json_err_log_* (same suffixes as the directives for location logging), should be used for this case.

Additionally, the variable $http_json_err_log_req will be set with a base64 encodede string for the data sent from the client, until the limit set by **http_json_log_req_body_limit** is reached.

#### Example Configuration


##### A simple configuration example

```yaml
     json_log_format my_log '
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
        a:i:list                    1;
        a:list                      string;
     ';

     json_log file:/tmp/log my_log;'
     json_log syslog:server=unix:/dev/log my_log;'
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
  },
  "list": [
    1,
    "string"
  ]
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

       json_log_format with_perl '
            comm.http.server_name       $server_name;
            perl.bar                    $bar;
       ';
       json_log file:/tmp/log with_perl'
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

##### A example for stream servers
```yaml
       json_log_format stream '
        _date                       $time_iso8601;
        src.addr                    $remote_addr;
        src.port                    $remote_port
        session.bytes.received      $bytes_received;
        session.bytes.sent          $bytes_received;
        session.connection          $connection;
        session.protocol            $protocol;
        session.time                $session_time;
        session.status              $status;
       ';
       json_log file:/tmp/stream.log stream'
 ```

 ```json

{
  "_date": "2017-04-09T20:52:41+00:00",
  "session": {
    "bytes": {
      "received": "23",
      "sent": "23"
    },
    "connection": "1",
    "protocol": "TCP",
    "status": "200",
    "time": "6.875"
  },
  "src": {
    "addr": "127.0.0.1"
  }
}


 ```

### Directives


* Syntax: **json_log_format** _format_name_ { _format_ } _if_=...;
* Default: —
* Context: http main
* Context: stream main

###### _format_name_ ######

Specifies a format name that can be used by a **json_log** directive

###### _format_ ######

See details above.

###### _if_=... ######

Works the same way as _if_ argument from http [access_log](http://nginx.org/en/docs/http/ngx_http_log_module.html#access_log) directive.

---

* Syntax: **json_log** _location_ _format_name_
* Default: —
* Context: http location
* Context: stream server


###### _location_ ######

Specifies the location for the output...

The output location type should be prefixed with supported location types. ( **file:** or **kafka:** )

For a **file:** type the value part will be a local file name. e.g. **file:**/tmp/log

For a **kafka:** type the value part will be the topic name. e.g. **kafka:** topic

The kafka output only happens if a list of brokers is defined by **json_log_kafka_brokers** directive.

###### _format_name_ ######

The format to use when writing to output destination.

---

* Syntax: **json_log_kafka_brokers** list of brokers separated by spaces;
* Default: —
* Context: http main
* Context: stream main

---

* Syntax: **json_log_kafka_client_id** _id_;
* Default: nginx
* Context: http main
* Context: stream main

---

* Syntax: **json_log_kafka_compression** _compression_codec_;
* Default: snappy
* Context: http main
* Context: stream main

---

* Syntax: **json_log_kafka_log_level** _numeric_log_level_;
* Default: 6
* Context: http main
* Context: stream main

---

* Syntax: **json_log_kafka_max_retries** _numeric_;
* Default: 0
* Context: http main
* Context: stream main

---

* Syntax: **json_log_kafka_buffer_max_messages** _numeric_;
* Default: 100000
* Context: http main
* Context: stream main

---

* Syntax: **json_log_kafka_backoff_ms** _numeric_;
* Default: 10
* Context: http main
* Context: stream main

---

* Syntax: **json_log_kafka_partition** _partition_;
* Default: RD_KAFKA_PARTITION_UA
* Context: http main
* Context: stream main

---

* Syntax: **http_json_log_req_body_limit** _size_;
* Default: 512
* Context: http local

Limits the body size to log.
Argument is a size string. May be 1k or 1M, but avoid this!


#### Error Directives

* Syntax: **json_err_log_format** _format_name_ { _format_ } _if_=...;
* Default: —
* Context: http main

----

* Syntax: **json_err_log** _location_ _format_name_
* Default: —
* Context: http server

----

* Syntax: **json_err_log_kafka_brokers** list of brokers separated by spaces;
* Default: —
* Context: http main

----

* Syntax: **json_err_log_kafka_client_id** _id_;
* Default: nginx
* Context: http server

----

* Syntax: **json_err_log_kafka_compression** _compression_codec_;
* Default: snappy
* Context: http server

----

* Syntax: **json_err_log_kafka_max_retries** _numeric_;
* Default: 0
* Context: http main


----

* Syntax: **json_err_log_kafka_log_level** _numeric_log_level_;
* Default: 6
* Context: http server

----

* Syntax: **json_err_log_kafka_buffer_max_messages** _numeric_;
* Default: 100000
* Context: http main

----

* Syntax: **json_err_log_kafka_backoff_ms** _numeric_;
* Default: 10
* Context: http main


----

* Syntax: **json_err_log_kafka_partition** _partition_;
* Default: RD_KAFKA_PARTITION_UA
* Context: http main


### Variables

#### $http_json_log_req_headers;

Creates a json object with all request headers.

Example:

```
    "req": {
        "headers": {
            "Host": "localhost",
            "User-Agent": "curl/7.52.1",
            "Accept": "*/*"
        }
    }
```

#### $http_json_log_req_body;

Log request body encoded as base64.
It requires proxy_pass configuration at logging location.

Example:

```
    "req": {
        "body": "Zm9v"
    }
```

#### $http_json_log_req_body_hexdump;

Log request body encoded as hexdump array.
It requires proxy_pass configuration at logging location.

Example:

```
    "hex": [
        "31 32 33 34 35 36 37 38 39  41 42 43 44 45 46 30 |123456789ABCDEF0|",
        "31 32 33 34 35 36 .. .. ..  .. .. .. .. .. .. .. |123456          |"
    ]
```

#### $http_json_log_resp_headers;

Creates a json object with available response headers.

Example:

```
        resp": {
        "headers": {
          "Last-Modified": "Sat, 01 Apr 2017 13:34:28 GMT",
          "ETag": "\"58dfac64-12\"",
          "X-Foo": "bar",
          "Accept-Ranges": "bytes"
        }
```

### Logging mail proxies authentication

If nginx is used for mail proxy and **auth_http** is set, then, it's possible to log the HTTP requests for authentication service.

An example with full configuration.

#### The mail proxy

```
mail {
    server_name mail.local;
    auth_http   localhost:9876/;

    server {
        listen    25;
        protocol  smtp;
        smtp_auth none;
    }
}
```

#### The HTTP auth service

```
http {
    server {
        listen 9876;

        location / {
            add_header Auth-Status OK;
            add_header Auth-Server 127.0.0.1;
            add_header Auth-Port 2525;

            root   html;
            index  index.html index.htm;

            json_log_format mail_log '
               _date                  $time_iso8601;
               headers                $http_json_log_req_headers;
            ';

            json_log file:/tmp/mail.log mail_log;
        }

        error_page 405 =200 $uri ;
        error_page 404 =200 /index.html ;
    }
}
```

#### The output log JSON line. ( pretty printed ...)

```
{
  "_date": "2017-04-12T23:58:07+00:00",
  "headers": {
    "Host": "localhost",
    "Auth-Method": "none",
    "Auth-User": "",
    "Auth-Pass": "",
    "Auth-Protocol": "smtp",
    "Auth-Login-Attempt": "1",
    "Client-IP": "127.0.0.1",
    "Client-Host": "[UNAVAILABLE]",
    "Auth-SMTP-Helo": "atacker.cloud",
    "Auth-SMTP-From": "mail from: <bob>",
    "Auth-SMTP-To": "rcpt to: <alice>"
  }
}
```

**Attention**: The Auth-Pass is not masked in any way, nor removed, so the client's value will be logged.



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
$ ./configure --add-module=/build/ngx_json_log
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
Files=1, Tests=12,  0 wallclock secs ( 0.02 usr  0.01 sys +  0.15 cusr  0.00 csys =  0.18 CPU)
Result: PASS

```

#### Load tests

Making 1M requests to local destination.

##### Logging to file


```
$ time ab -n 1000000 -c 4 http://127.0.0.1/mega
This is ApacheBench, Version 2.3 <$Revision: 1757674 $>
Copyright 1996 Adam Twiss, Zeus Technology Ltd, http://www.zeustech.net/
Licensed to The Apache Software Foundation, http://www.apache.org/

Benchmarking 127.0.0.1 (be patient)
Completed 100000 requests
Completed 200000 requests
Completed 300000 requests
Completed 400000 requests
Completed 500000 requests
Completed 600000 requests
Completed 700000 requests
Completed 800000 requests
Completed 900000 requests
Completed 1000000 requests
Finished 1000000 requests


Server Software:        nginx/1.10.3
Server Hostname:        127.0.0.1
Server Port:            80

Document Path:          /mega
Document Length:        169 bytes

Concurrency Level:      4
Time taken for tests:   95.224 seconds
Complete requests:      1000000
Failed requests:        0
Non-2xx responses:      1000000
Total transferred:      319000000 bytes
HTML transferred:       169000000 bytes
Requests per second:    10501.55 [#/sec] (mean)
Time per request:       0.381 [ms] (mean)
Time per request:       0.095 [ms] (mean, across all concurrent requests)
Transfer rate:          3271.48 [Kbytes/sec] received

Connection Times (ms)
              min  mean[+/-sd] median   max
Connect:        0    0   0.2      0      64
Processing:     0    0   0.4      0      98
Waiting:        0    0   0.4      0      97
Total:          0    0   0.5      0     115

Percentage of the requests served within a certain time (ms)
  50%      0
  66%      0
  75%      0
  80%      0
  90%      1
  95%      1
  98%      1
  99%      1
 100%    115 (longest request)

real   1m36.057s
user   0m5.390s
sys    1m22.040s

$ wc -l /tmp/1M.log
1000000 /tmp/1M.log
```

##### Logging to kafka topic

```
$ time ab -n 1000000 -c 4 http://127.0.0.1/mega
This is ApacheBench, Version 2.3 <$Revision: 1757674 $>
Copyright 1996 Adam Twiss, Zeus Technology Ltd, http://www.zeustech.net/
Licensed to The Apache Software Foundation, http://www.apache.org/

Benchmarking 127.0.0.1 (be patient)
Completed 100000 requests
Completed 200000 requests
Completed 300000 requests
Completed 400000 requests
Completed 500000 requests
Completed 600000 requests
Completed 700000 requests
Completed 800000 requests
Completed 900000 requests
Completed 1000000 requests
Finished 1000000 requests


Server Software:        nginx/1.10.3
Server Hostname:        127.0.0.1
Server Port:            80

Document Path:          /mega
Document Length:        169 bytes

Concurrency Level:      4
Time taken for tests:   102.439 seconds
Complete requests:      1000000
Failed requests:        0
Non-2xx responses:      1000000
Total transferred:      319000000 bytes
HTML transferred:       169000000 bytes
Requests per second:    9761.95 [#/sec] (mean)
Time per request:       0.410 [ms] (mean)
Time per request:       0.102 [ms] (mean, across all concurrent requests)
Transfer rate:          3041.08 [Kbytes/sec] received

Connection Times (ms)
              min  mean[+/-sd] median   max
Connect:        0    0   0.3      0      93
Processing:     0    0   2.1      0    1021
Waiting:        0    0   2.1      0    1021
Total:          0    0   2.1      0    1022

Percentage of the requests served within a certain time (ms)
  50%      0
  66%      0
  75%      0
  80%      0
  90%      1
  95%      1
  98%      1
  99%      2
 100%   1022 (longest request)

real   1m43.328s
user   0m5.770s
sys    1m21.380s
```

#### Docker

Docker images and a docker compose file is available at the ./docker directory.


```
$ docker-compose up --build -d

Creating nginx-json-zookeeper
Creating nginx-json-kafka
Creating nginx-json

```

...


```
$ docker-compose ps
        Name                      Command               State                         Ports
------------------------------------------------------------------------------------------------------------------
nginx-json             /bin/sh -c /usr/local/ngin ...   Up      0.0.0.0:80->80/tcp
nginx-json-kafka       start-kafka.sh                   Up      0.0.0.0:9092->9092/tcp
nginx-json-zookeeper   /bin/sh -c /usr/sbin/sshd  ...   Up      0.0.0.0:2181->2181/tcp, 22/tcp, 2888/tcp, 3888/tcp

```

An additional docker service for development it's available.

Just uncomment the nginx-json-dev service block.

Also docker services for the ELK stack are available.


```
         Name                        Command               State                                   Ports
------------------------------------------------------------------------------------------------------------------------------------------
nginx-json                 /bin/sh -c /usr/local/ngin ...   Up      0.0.0.0:80->80/tcp
nginx-json-dev             /bin/bash                        Up      0.0.0.0:81->81/tcp
nginx-json-elasticsearch   /bin/bash bin/es-docker          Up      0.0.0.0:9200->9200/tcp, 0.0.0.0:9300->9300/tcp
nginx-json-kafka           start-kafka.sh                   Up      0.0.0.0:9092->9092/tcp
nginx-json-kibana          /bin/sh -c /usr/local/bin/ ...   Up      0.0.0.0:5601->5601/tcp
nginx-json-logstash        /usr/local/bin/docker-entr ...   Up      0.0.0.0:5044->5044/tcp, 0.0.0.0:5050->5050/tcp, 0.0.0.0:9600->9600/tcp
nginx-json-zookeeper       /bin/sh -c /usr/sbin/sshd  ...   Up      0.0.0.0:2181->2181/tcp, 22/tcp, 2888/tcp, 3888/tcp

```

