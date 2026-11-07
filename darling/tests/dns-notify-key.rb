# Run Darling's dns_configuration_notify_key and compare it with the key in
# Apple's dnsinfo/dnsinfo_copy.c from the same repository.
# Usage: ruby darling/tests/dns-notify-key.rb [path/to/repository]
require 'tmpdir'
root = File.expand_path(ARGV[0] || '../..', __dir__)
apple = File.read(File.join(root, 'dnsinfo/dnsinfo_copy.c'))
expected = apple[/^dns_configuration_notify_key\(\)\n\{.*?key = "([^"]+)";/m, 1] or
  abort "Apple's key not found"
source = File.read(File.join(root, 'darling/src/dnsinfo/dnsinfo.c'))
body = source[/^const char \*\ndns_configuration_notify_key\s*\(\)\n\{.*?^\}/m] or
  abort "Darling's dns_configuration_notify_key not found"
program = <<~C
  #include <stdio.h>
  #include <string.h>
  #{body}
  int main(void) {
      const char *key = dns_configuration_notify_key();
      if (key == NULL || strcmp(key, "#{expected}") != 0) {
          printf("FAIL: got %s, expected #{expected}\\n", key ? key : "NULL");
          return 1;
      }
      puts("PASS: dns_configuration_notify_key returns Apple's key");
      return 0;
  }
C
Dir.mktmpdir('dns-notify-key') do |dir|
  input = File.join(dir, 'probe.c'); output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-Wall', input, '-o', output)
  abort 'dns notify key regression' unless system(output)
end
