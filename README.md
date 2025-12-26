Stanford CS 144 Networking Lab
==============================

These labs are open to the public under the (friendly, but also mandatory)
condition that to preserve their value as a teaching tool, solutions not
be posted publicly by anybody.

Website: https://cs144.stanford.edu

To set up the build system: `cmake -S . -B build`

To compile: `cmake --build build`

To run tests: `cmake --build build --target check[checkpoint_num]`

or `cmake --build build --target test`

To run speed benchmarks: `cmake --build build --target speed`

To run clang-tidy (which suggests improvements): `cmake --build build --target tidy`

To format code: `cmake --build build --target format`

总结:

1. check0 实现 bytestring
2. check1 实现 reassemble
3. check2 实现 wrapping_integer, tcp_receiver
4. check3 实现 tcp_sender, 也是实现的比较简单，只用超时重传就好了（我一开始以为cs144会实现拥塞控制、状态机之类的，发现还是简化了）。
5. check4 我啥也没干，他说要调研 RTO, 然后写一个报告之类的
6. check5 实现 arp
7. check6 实现 路由表
8. check7 我也啥没干，他他说要找小伙伴一起搭建一个虚拟网络
