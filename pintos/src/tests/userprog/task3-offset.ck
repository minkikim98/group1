# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(task3-offset) begin
(task3-offset) end
task3-offset: exit(0)
EOF
pass;
