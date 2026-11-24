# Whole project.  Build both generations: make      Clean: make clean
all:
	$(MAKE) -C module1
	$(MAKE) -C module2

module1:
	$(MAKE) -C module1

module2:
	$(MAKE) -C module2

clean:
	$(MAKE) -C module1 clean
	$(MAKE) -C module2 clean

.PHONY: all clean module1 module2
