# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(ourwaittest) begin
child: exit(5)
(child-simple) run
child-simple: exit(81)
(ourwaittest) wait(exec()) = -1
(ourwaittest) end
ourwaittest: exit(0)
EOF
pass;
