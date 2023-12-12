
UNIT_FOLDER = ../src
TEST_LIST = regmap_test
INC = . ../include ../system/include

all: $(TEST_LIST)

%_test: %_test.c $(UNIT_FOLDER)/%.c
	gcc $(addprefix -D, $(DEFS)) $(addprefix -I, $(INC)) $^ -o $@
	./$@

clean:
	rm -f $(TEST_LIST)
