.PHONY:	all

BENCHS = src/linkedlist
LBENCHS = src/linkedlist-lock
LFBENCHS = src/linkedlist
LCPPBENCHS = src/linkedlist-cpp


.PHONY:	clean all $(BENCHS) $(LBENCHS) $(LCPPBENCHS)

all:	lockfree lock lockfreecpp

lock:
	$(MAKE) "LOCK=LOCKTYPE" $(LBENCHS)

lockfree:
	$(MAKE) "STM=LOCKFREE" $(LFBENCHS)

lockfreecpp:
	$(MAKE) $(LCPPBENCHS)

clean:
	$(MAKE) -C src/linkedlist clean	
	$(MAKE) -C src/linkedlist-lock clean
	$(MAKE) -C src/linkedlist-cpp clean
	rm -rf build

$(BENCHS):
	$(MAKE) -C $@ $(TARGET)

$(LBENCHS):
	$(MAKE) -C $@ $(TARGET)

$(LFBENCHS):
	$(MAKE) -C $@ $(TARGET)


$(LCPPBENCHS):
	$(MAKE) -C $@ $(TARGET)
