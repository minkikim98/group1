# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(buf_cache_1) begin
(buf_cache_1) Hit rate with a cold cache: 41 / 51
(buf_cache_1) Hit rate for re-opened file: 51 / 51
(buf_cache_1) end
EOF
pass;
