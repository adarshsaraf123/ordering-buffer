#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <cstdlib>
#include <thread>

#define BLOCKS_BUFFER_SIZE 100

using namespace std;

class Block {
	int number_;
	public:

	Block(int n): number_(n) {
	}

	int number() {
		return number_;
	}
};

/* 
 * BlocksBuffer is a buffer for storing out-of-order blocks and sending them in the correct sequence.
 * Interface:
 * 	1. AddBlock: for adding a new block to the buffer
 * 	2. GetCurrentBlock: for getting the next block as specified by block_to_send. This also increments 
 * 		block_to_send.
 * 	3. UpdateBlockToSend: for updating the block to be sent. This can be used by the consumer to tell
 * 		the buffer what block it is expecting.
 */
class BlocksBuffer {
	unsigned int buffer_size;           // size of the buffer
	vector< pair<Block, int> > buffer;  // the first element should ideally be a block
	int current_blocknum;               // number of the current block that ought to be delivered by the buffer
	mutex current_blocknum_mutex;       // mutex to guard the current_blocknum access
	mutex current_slot_mutex;           // current slot is the slot for the current block in the buffer
	condition_variable current_slot_cv; // condition variable to wake up once the current slot is filled

	public:
	
	// constructor with initialization list
	BlocksBuffer(unsigned int bsize): buffer_size(bsize), 
			buffer(buffer_size, make_pair(Block(0), -1)),  // init the buffer with the default block and epoch -1
			current_blocknum(0) {
	}

	void  AddBlock(Block b);

	Block GetCurrentBlock();

	void  SetCurrentBlocknum(int blocknum);
};

/* 
 * BlocksBuffer::AddBlock 
 * 	- adds an arbitrary block to the buffer.
 *
 * It assumes that the value at its index in the buffer will no longer be used and is replaceable. 
 *
 * Of course for this assumption to work the buffer should be large enough such that the 
 * 	producer cannot produce `buffer_size` items before the consumer can consume an item.
 * 	If that is not the case, then we will end up replacing an item before the consumer could 
 * 	consume it.
 */
void BlocksBuffer::AddBlock(Block b) {
	int epoch = b.number() / buffer_size;
	int index = b.number() % buffer_size;
	// we don't protect access to current_blocknum here since GetCurrentBlock is already doing it 
	//  for us
	if (b.number() == current_blocknum) {
		// acquire the current_slot_mutex
		lock_guard<mutex> lck(current_slot_mutex);
		buffer[index] = make_pair(b, epoch);
		current_slot_cv.notify_one();
	} else {
		buffer[index] = make_pair(b, epoch);
	}
}

/* 
 * BlocksBuffer::GetCurrentBlock 
 * 	- gets the current block from the buffer.
 *
 * It blocks until the current block becomes available.
 *
 * While getting the current block, we must not allow the current_blocknum to be changed
 * 	else it could happen that GetCurrentBlock is waiting for a different block while 
 * 	AddBlock notifies on a different block. Therefore the current_blocknum is unchanged throughout
 * 	the lifetime of GetCurrentBlock.
 *
 * NOTE: SetCurrentBlocknum blocks throughout the duration of GetCurrentBlock. Removing this is a lot more
 * 		painful but is desirable since it may happen that GetCurrentBlock is waiting for block 15 
 * 		while meanwhile the consumer only wants block 10 which is actually available in the buffer.
 * 		We therefore end up waiting unnecessarily for block 15 and delay the consumer.
 */
Block BlocksBuffer::GetCurrentBlock() {
	// Acquire the current_blocknum_mutex to prevent SetCurrentBlocknum from changing the same
	cout << "expected blocknum: " << current_blocknum << endl << flush;
	lock_guard< mutex > blocknum_lck(current_blocknum_mutex);
	int epoch = current_blocknum / buffer_size;
	int index = current_blocknum % buffer_size;

	// Acquire the current_slot_mutex to prevent AddBlock from modifying the same
	unique_lock<mutex> slot_lck(current_slot_mutex);
	// check if the current slot holds the current block by means of the epoch
	if(buffer[index].second == epoch) {
		current_blocknum++;
		return buffer[index].first;
	} else {
		// this releases the current_slot_mutex for AddBlock to access the slot
		current_slot_cv.wait(slot_lck);
		current_blocknum++;
		return buffer[index].first;
	}
}

/* 
 * BlocksBuffer::SetCurrentBlocknum
 * 	- update the current_blocknum
 * It is called by the consumer to tell the buffer the block it wants from the buffer.
 * Since this could cause a race condition in the access of current_blocknum, we protect the same 
 * 	using the current_blocknum_mutex
 */
void BlocksBuffer::SetCurrentBlocknum(int blocknum) {
	lock_guard< mutex > lck(current_blocknum_mutex);
	current_blocknum = blocknum;
}

BlocksBuffer buffer(BLOCKS_BUFFER_SIZE);

void add_random_blocks() {
	int n;
	srand(time(NULL));
	while(true) {
		n = rand()%BLOCKS_BUFFER_SIZE;
		cout << n << " " << flush;
		buffer.AddBlock(n);
		this_thread::sleep_for(chrono::milliseconds(100));
	}
}

int main() {
	Block b(-1);
	thread t(add_random_blocks);
	while(b.number() != BLOCKS_BUFFER_SIZE - 1) {
		b = buffer.GetCurrentBlock();
		cout << "\nOut: " << b.number() << endl;
	}
}
