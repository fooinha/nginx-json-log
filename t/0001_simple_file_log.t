use Test::Nginx::Socket 'no_plan';
use Test::Nginx::Util qw/$ServRoot/;

sub check_file_contents {
   my ($file_to_read, $expected) = @_;
   my $file = "$ServRoot/$file_to_read";

   CORE::chomp $file;

   open my $fh, $file or die "Can't open '$file' for input:\n$!";
   my $data = CORE::join '', <$fh>;
   close $fh;

   CORE::chomp $data;
   CORE::chomp $expected;

   unlink $file;

   die "\n\n[$data] does not match [$expected]\n\n" if ($data ne $expected);
}

sub check_file_test_1 {
   check_file_contents('test.1.json', '{"literal":"root"}');
}

sub check_file_test_2 {
   check_file_contents('test.2.json', '{"false":false,"int":2014,"literal":"root","null":null,"real":1.1,"true":true}');
}

run_tests();

__DATA__

=== TEST 1: single string literal value
--- config
      location /kasha {
            return 200 "hello";
            http_log_json_format file:test.1.json 'literal root;';
     }
--- request
    GET /kasha
--- response_body_filters eval
\&main::check_file_test_1
--- error_code: 200

=== TEST 2: literal values
--- config
      location /kasha {
            return 200 "hello";
            http_log_json_format file:test.2.json '
               b:true         true;
               b:false        false;
               n:null         whatever;
               r:real         1.1;
               i:int          2014;
            literal root;
     ';
     }
--- request
    GET /kasha
--- response_body_filters eval
\&main::check_file_test_2
--- error_code: 200

