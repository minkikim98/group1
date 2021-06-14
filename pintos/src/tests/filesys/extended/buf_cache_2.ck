# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(buf_cache_2) begin
(buf_cache_2) end
EOF
pass;
