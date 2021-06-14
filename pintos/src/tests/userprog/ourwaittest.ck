# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(ourwaittest) begin
(child-simple) run
child-simple: exit(81)
child: exit(5)
(ourwaittest) wait(exec()) = -1
(ourwaittest) end
ourwaittest: exit(0)
EOF
pass;
