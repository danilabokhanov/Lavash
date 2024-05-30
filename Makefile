lavash: lavash.cpp
	$(CXX) $^ -o $@

tools/%: tools/%.cpp
	$(CXX) $^ -o $@

tools: tools/print_args tools/print_envs

test: lavash test.py tools
	python3 test.py
