REMOTE_TARGET := root@cheribsd:/intravisor

all :
	@$(MAKE) -C src

sync:
	# scp $$(git diff --name-only  | cat | xargs printf '%s ') ${REMOTE_TARGET}/monitor_src/
	scp -r src/*.[ch] src/tfork ${REMOTE_TARGET}/monitor_src/
	scp src/build/monitor ${REMOTE_TARGET}/monitor_dev 

clean:
	@$(MAKE) -C src clean

.PHONY: all clean sync
