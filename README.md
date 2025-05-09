# Lock Free Linked List Exploration
I'm trying to figure out what implementing a lock free linked list in
another project would look like. Here I implement 3 algorithms from 3
different papers and a lock version for comparison.

It is worth noting that Harris and Michael implement ordered linked lists,
but I didn't. Zhang's is explicitly an unordered only linked list.

All algorithms are lock-free, not wait-free. Zhang specifies a wait-free
algorithm in the paper, but it is not something I think would perform
better in my use case. It would be good if you care about maximum latency
because it ensures no individual thread can starve indefinitely.

This uses [ck](https://github.com/concurrencykit/ck) and [pf](https://github.com/carterww/pf).
Memory ordering may be off for other platforms. This is only prototyping
code, so I didn't put much thought into how things would behave on non TSO
memory model architectures.

## Implementations

### [Harris](https://timharris.uk/papers/2001-disc.pdf)
Probably the most well known implementation that Michael heavily builds off.
I didn't add anything for memory reclamation, just implemented the list. It
seems to perform about as well as the others.

### [Zhang](https://cic.tju.edu.cn/faculty/zhangkl/web/aboutme/disc13-tr.pdf)
A more recent lock free list implementation that is probably much simpler
than Harris's and doesn't require a marked pointer. This makes working with
it much safer. I implemented a version (zhang2.c) that did put this state
information in the 2 low bits of the pointer.

One downside of this algorithm is that it requires "dummy" nodes to delete
nodes. This isn't terrible, but it does require more memory allocation. Inserts
also have to (not strictly) traverse the entire list. If they don't, my tests
show it is much faster than Harris's but the much of the list is filled with
INV nodes.

I think the major upside to this algorithm is its simplicity. It is surprisingly
easy to reason about compared to Harris's or Michael's list (at least for me).

### [Michael](https://docs.rs/crate/crossbeam/0.2.4/source/hash-and-skip.pdf)
This essentially used Harris's algorithm with hazard pointers. One of the major
problems with Harris's original algorithm is that is required a tag and DCAS
to safely reclaim memory and avoid the ABA problem. Michael showed how the tag
could be removed if hazard pointers are used.

## Thoughts
~~Before I make a decision between the Zhang or Michael-Harris list I'd like to
add hazard pointers to the Zhang implementation and run some better
benchmarks. My use case will be a very read heavy list (\~80%+) and I don't
even benchmark the lists with reads.~~

~~If the benchmarks are pretty close I will probably go with Zhang's for 3 reasons:~~
1. ~~It is very easy to reason about.~~
2. ~~The pointers can be safe by default (no oops I forgot to unmark() this pointer).~~
3. ~~The retired nodes list will require an extra retired_next member anyway. This
   can act as the state field when the node is still valid.~~

It turns out Zhang's algorithm is not compatible with hazard pointers and requires
a garbage collector :(. I incorrectly implemented it in the benchmark just to
get results.

This occurs because Zhang's algorithm allows INV nodes to be reinserted back into
the list if two adjacent nodes are removed. The algorithm *could* work with
hazard pointers but you'd have to handle duplicate nodes in the retire list.
