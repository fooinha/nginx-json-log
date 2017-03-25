use Test::Nginx::Socket 'no_plan';
use Test::Nginx::Util qw/$ServRoot/;
use JSON;
use Test::More;

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
}

sub check_file_test_1 {
   check_file_contents('test.1.json', '{"literal":"root"}');
}

sub check_file_test_2 {
   my $json_text = get_file_contents('test.2.json');

   my $scalar = from_json( $json_text, { utf8  => 1 } );

   ok(not defined($scalar->{'null'}));
   ok($scalar->{'real'} eq '1.1');
   ok($scalar->{'int'} eq '2014');
   ok($scalar->{'literal'} eq 'root');
   is($scalar->{'true'}, JSON::true);
   is($scalar->{'false'}, JSON::false);
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

