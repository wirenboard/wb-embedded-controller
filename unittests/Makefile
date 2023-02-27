
UNIT_FOLDER = ../src
TEST_LIST = regmap_test
SUBMODULES_DIR = ../libfixmath/libfixmath
INC = ../include $(SUBMODULES_DIR)

all: $(TEST_LIST)

%_test: %_test.c $(UNIT_FOLDER)/%.c $(SUBMODULES_DIR)/*.c
	gcc $(addprefix -D, $(DEFS)) $(addprefix -I, $(INC)) $^ -o $@
	./$@
	rm -f $@

clean:
	rm -f $(TEST_LIST)
