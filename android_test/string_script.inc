R"(
[vertex shader binary]
7230203 10000 80004 18 0 20011 1 6000b 1 4c534c47 6474732e 3035342e 0 3000e 0 1
9000f 0 4 6e69616d 0 d 11 15 16 30003 2 1ae 40005 4 6e69616d 0 60005 b 505f6c67
65567265 78657472 0 60006 b 0 505f6c67 7469736f 6e6f69 70006 b 1 505f6c67
746e696f 657a6953 0 70006 b 2 435f6c67 4470696c 61747369 65636e 30005 d 0 50005
11 69736f70 6e6f6974 0 50005 15 6f6c6f63 756f5f72 74 50005 16 6f6c6f63 6e695f72
0 50048 b 0 b 0 50048 b 1 b 1 50048 b 2 b 3 30047 b 2 40047 11 1e 0 40047 15 1e
0 40047 16 1e 1 20013 2 30021 3 2 30016 6 20 40017 7 6 4 40015 8 20 0 4002b 8 9
1 4001c a 6 9 5001e b 7 6 a 40020 c 3 b 4003b c d 3 40015 e 20 1 4002b e f 0
40020 10 1 7 4003b 10 11 1 40020 13 3 7 4003b 13 15 3 4003b 10 16 1 50036 2 4 0
3 200f8 5 4003d 7 12 11 50041 13 14 d f 3003e 14 12 4003d 7 17 16 3003e 15 17
100fd 10038

[fragment shader binary]
7230203 10000 80004 d 0 20011 1 6000b 1 4c534c47 6474732e 3035342e 0 3000e 0 1
7000f 4 4 6e69616d 0 9 b 30010 4 7 30003 2 1ae 40005 4 6e69616d 0 50005 9
6f6c6f63 756f5f72 74 50005 b 6f6c6f63 6e695f72 0 40047 9 1e 0 40047 b 1e 0 20013
2 30021 3 2 30016 6 20 40017 7 6 4 40020 8 3 7 4003b 8 9 3 40020 a 1 7 4003b a b
1 50036 2 4 0 3 200f8 5 4003d 7 c b 3003e 9 c 100fd 10038

[vertex data]
# position      color
0/R32G32_SFLOAT 1/A8B8G8R8_UNORM_PACK32

# Top-left red
-1 -1           0xff0000ff
0  -1           0xff0000ff
-1 0            0xff0000ff
0  -1           0xff0000ff
-1 0            0xff0000ff
0  0            0xff0000ff
# Top-right green
0 -1            0xff00ff00
1  -1           0xff00ff00
0 0             0xff00ff00
1  -1           0xff00ff00
0 0             0xff00ff00
1  0            0xff00ff00
# Bottom-left blue
-1 0            0xffff0000
0  0            0xffff0000
-1 1            0xffff0000
0  0            0xffff0000
-1 1            0xffff0000
0  1            0xffff0000
# Bottom-right purple
0 0             0xff800080
1 0             0xff800080
0 1             0xff800080
1 0             0xff800080
0 1             0xff800080
1 1             0xff800080

[test]
clear

draw arrays TRIANGLE_LIST 0 24

relative probe rect rgb (0, 0, 0.5, 0.5) (1, 0, 0)
relative probe rect rgb (0.5, 0, 0.5, 0.5) (0, 1, 0)
relative probe rect rgb (0, 0.5, 0.5, 0.5) (0, 0, 1)
relative probe rect rgb (0.5, 0.5, 0.5, 0.5) (0.5, 0, 0.5)
)"
