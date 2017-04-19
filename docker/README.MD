Available services.


          Name                        Command               State                                   Ports
------------------------------------------------------------------------------------------------------------------------------------------
nginx-json                 /bin/sh -c /usr/local/ngin ...   Up      0.0.0.0:80->80/tcp
nginx-json-dev             /bin/bash                        Up      0.0.0.0:81->81/tcp
nginx-json-elasticsearch   /bin/bash bin/es-docker          Up      0.0.0.0:9200->9200/tcp, 0.0.0.0:9300->9300/tcp
nginx-json-kafka           start-kafka.sh                   Up      0.0.0.0:9092->9092/tcp
nginx-json-kibana          /bin/sh -c /usr/local/bin/ ...   Up      0.0.0.0:5601->5601/tcp
nginx-json-logstash        /usr/local/bin/docker-entr ...   Up      0.0.0.0:5044->5044/tcp, 0.0.0.0:5050->5050/tcp, 0.0.0.0:9600->9600/tcp
nginx-json-zookeeper       /bin/sh -c /usr/sbin/sshd  ...   Up      0.0.0.0:2181->2181/tcp, 22/tcp, 2888/tcp, 3888/tcp


While running in linux, it might be required to set mmap count, in order, to start elasticsearch service.

- https://www.elastic.co/guide/en/elasticsearch/reference/current/vm-max-map-count.html
- https://github.com/docker-library/elasticsearch/issues/111

```
sudo sysctl -w vm.max_map_count=262144
```
