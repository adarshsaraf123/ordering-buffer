ordering_buffer: ordering_buffer.cpp 
	g++ -std=c++11 -o $@ $^ -lpthread

clean:
	rm ordering_buffer
