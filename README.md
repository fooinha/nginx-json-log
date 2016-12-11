# ngx-kasha


nginx module for advanced per location logging - (aka kasha 🍲)

## Description

This module adds to nginx the ability of advanced JSON logging of HTTP requests per location.
It's possible to log to a destination ouput any request made to a specific nginx location.
The output format is configurable.

### Configuration

Each logging configuration is based on a kasha_recipe. (🍲)


A kasha recipe is a ';' separated list of items to include in the logging preparation.

The left hand side part of item will be the JSON Path for the variable name
The left hand side part can be prefixed with 's:', 'i:' or 'r:', so the JSON encoding type can be controlled.

* 's:' - JSON string ( default )
* 'i:' - JSON integer
* 'r:' - JSON real


The right hand side will be the variable's name or literal value.
For this, known or previously setted variables, can be used by using the '$' before name.

Common HTTP nginx builtin variables like $uri, or any other variable set by other handler modules can be used.

The output is sent to the location specified by the first kasha_recipe argument.
For now, the only supported location is "file:".

#### Example Configuration


##### A simple configuration example

```yaml
	kasha_recipe file:/tmp/log '
		src.ip                      $remote_addr;
		src.port                    $remote_port;
		dst.ip                      $server_addr;
		dst.port                    $server_port;
		_date                       $time_iso8601;
		r:_real                     1.1;
		i:_int                      2016;
		i:_status                   $status;
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

       kasha_recipe file:/tmp/log '
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

* Syntax: kasha_recipe location { recipe };
* Default: 	—
* Context: http

The 'file:' it's the only valid location prefix, and determines that the logging location will be a local filesystem file.


### Build

#### Dependencies

* [libjansson](http://www.digip.org/jansson/)

For Ubuntu or Debian install development packages.

```bash
$ sudo apt-get install libjansson-dev

```

Build as a common nginx module.

```bash
$ ./configure --add-module=/build/ngx-kasha
$ make && make install

```



### Tests and Fair Warning

**THIS IS NOT PRODUCTION** ready.

This was done over the weekend as a proof of concept, and it also lacks unit tests.

So there's no guarantee of success. It most probably blow up when running in real life scenarios.


