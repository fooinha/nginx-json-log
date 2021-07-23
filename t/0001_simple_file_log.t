use Test::Nginx::Socket 'no_plan';
use Test::Nginx::Util qw/$ServRoot/;
use JSON;
use Test::More;
use Data::Dumper;

sub get_file_contents {
   my ($file_to_read) = @_;
   my $file = "$ServRoot/$file_to_read";

   CORE::chomp $file;

   open my $fh, $file or die "Can't open '$file' for input:\n$!";
   my $data = CORE::join '', <$fh>;
   close $fh;

   CORE::chomp $data;

   unlink $file;
   return $data;
}

sub check_file_contents {
   my ($file, $expected) = @_;

   my $data = get_file_contents($file);

   die "\n\n[$data] does not match [$expected]\n\n" if ($data ne $expected);
   1
}

sub check_file_test_1 {
   ok(check_file_contents('test.1.json', '{"literal":"root"}'),
       "'literal' key has 'root' value");
}

sub check_file_test_2 {
   my $json_text = get_file_contents('test.2.json');
   my $scalar = from_json( $json_text, { utf8  => 1 } );

   ok(! defined($scalar->{'null'}), "null key");
   ok($scalar->{'real'} eq '1.1', "Literal real value is 1.1");
   ok($scalar->{'int'} eq '2014', "Literal int value is 2014");
   ok($scalar->{'literal'} eq 'root', "Literal string value is 'root'");
   is($scalar->{'true'}, JSON::true, "Literal true value confirmed");
   is($scalar->{'false'}, JSON::false, "Literal false value confirmed");
}

sub check_file_test_3 {
   my $size = -s "$ServRoot/test.3.json";
   unlink "$ServRoot/test.3.json";
   die ("File should be empty") if ($size ne 0);
}

sub check_file_test_4 {
   my $json_text = get_file_contents('test.4.json');

   my $scalar = from_json( $json_text, { utf8  => 1 } );

   is(ref($scalar->{'list'}), 'ARRAY', "List is an ARRAY");

   my $expected_len = 2;
   my $len = scalar(@{$scalar->{'list'}});
   ok($expected_len eq $len, "Array Lists $len = $expected_len" );

   unlink "$ServRoot/test.4.json";
}

sub check_file_test_5 {
   my $json_text = get_file_contents('test.5.json');

   my $scalar = from_json( $json_text, { utf8  => 1 } );

   is(ref($scalar->{'headers'}), 'HASH');
   ok($scalar->{'headers'}->{'Host'} eq 'localhost', "Host header is 'localhost'");
   ok($scalar->{'headers'}->{'Connection'} eq 'close', "Connection header is 'close'");

   unlink "$ServRoot/test.5.json";
}

sub check_file_test_6 {
   my $json_text = get_file_contents('test.6.json');

   my $scalar = from_json( $json_text, { utf8  => 1 } );

   ok(defined($scalar->{'headers'}), "has headers key");
   ok($scalar->{'headers'} =~ /HTTP\/1.1 200 OK/, 'headers value has "OK"');

   unlink "$ServRoot/test.6.json";
}

sub check_file_test_7 {
   my $json_text = get_file_contents('test.7.json');

   my $scalar = from_json( $json_text, { utf8  => 1 } );

   ok($scalar->{'method'} eq "POST", "client method is POST");
   ok($scalar->{'body_plain'} eq "foo=bar", "body not encoded is 'foo=bar'");
   ok($scalar->{'body_base64'} eq "Zm9vPWJhcg==", "body in base64");
   ok($scalar->{'body_hex'} eq "666f6f3d626172", "body in hex");

   my @exp_hexdump = ('66 6f 6f 3d 62 61 72 .. ..  .. .. .. .. .. .. .. |foo=bar         |');
   ok(@{$scalar->{'body_hexdump'}} ~~ @exp_hexdump, "body in hexdump format");

   unlink "$ServRoot/test.7.json";
}

run_tests();

__DATA__

=== TEST 1: single string literal value
--- http_config
    json_log_format json_1 'literal root;';
--- config
        location /kasha {
            return 200 "hello";
            json_log file:test.1.json json_1;
        }
--- request
GET /kasha
--- response_body_filters eval
\&main::check_file_test_1
--- error_code: 200

=== TEST 2: literal values
--- http_config
    json_log_format json_2 '
       b:true         true;
       b:false        false;
       n:null         whatever;
       r:real         1.1;
       i:int          2014;
       literal        root;
    ';
--- config
        location /kasha {
            return 200 "hello";
            json_log file:test.2.json json_2;
        }
--- request
GET /kasha
--- response_body_filters eval
\&main::check_file_test_2
--- error_code: 200

=== TEST 3: if condition
--- http_config
    json_log_format json_3 '
       b:true         true;
       b:false        false;
       n:null         whatever;
       r:real         1.1;
       i:int          2014;
       literal root;
    ' if=0;
--- config
        location /kasha {
            return 200 "hello";

            json_log file:test.3.json json_3;
        }
--- request
    GET /kasha
--- response_body_filters eval
\&main::check_file_test_3
--- error_code: 200

=== TEST 4: arrays
--- http_config
    json_log_format json_4 '
       a:i:list       1;
       a:list     string;
    ';
--- config
        location /kasha {
            return 200 "hello";
            json_log file:test.4.json json_4;
        }
--- request
    GET /kasha
--- response_body_filters eval
\&main::check_file_test_4
--- error_code: 200

=== TEST 5: request headers
--- http_config
    json_log_format json_5 '
       headers        $http_json_log_req_headers;
    ';
--- config
        location /kasha {
            return 200 "hello";
            json_log file:test.5.json json_5;
        }
--- request
    GET /kasha
--- response_body_filters eval
\&main::check_file_test_5
--- error_code: 200

=== TEST 6: response headers
--- http_config
    json_log_format json_6 '
       headers        $http_json_log_resp_headers;
    ';
--- config
        location /kasha {
            return 200 "hello";
            json_log file:test.6.json json_6;
        }
--- request
    GET /kasha
--- response_body_filters eval
\&main::check_file_test_6
--- error_code: 200


=== TEST 7: request POST body

--- http_config
    server {
        listen 9999;
        location / {
            return 204;
        }
    }

    json_log_format json_7 '
       method               $request_method;
       body_plain           $http_json_log_req_body;
       body_hex|hex         $http_json_log_req_body;
       body_hexdump|hexdump $http_json_log_req_body;
       body_base64|base64   $http_json_log_req_body;
    ';
--- config
        location /kasha {
            proxy_pass http://127.0.0.1:9999/;
            json_log file:test.7.json json_7;
        }
--- request
    POST /kasha
foo=bar
--- response_body_filters eval
\&main::check_file_test_7
--- error_code: 204
