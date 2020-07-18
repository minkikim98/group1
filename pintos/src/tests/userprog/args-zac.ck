# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(args) begin
(args) argc = 17
(args) argv[0] = 'args-zac'
(args) argv[1] = 'I'
(args) argv[2] = 'am'
(args) argv[3] = 'absolutely'
(args) argv[4] = 'the'
(args) argv[5] = 'most'
(args) argv[6] = 'op'
(args) argv[7] = 'person'
(args) argv[8] = 'in'
(args) argv[9] = 'the'
(args) argv[10] = 'existance'
(args) argv[11] = 'of'
(args) argv[12] = 'all'
(args) argv[13] = 'comprehension'
(args) argv[14] = 'in'
(args) argv[15] = 'its'
(args) argv[16] = 'entirety.'
(args) argv[17] = null
(args) end
args-zac: exit(0)
EOF
pass;