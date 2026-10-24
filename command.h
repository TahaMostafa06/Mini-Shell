
#ifndef command_h
#define command_h

struct SimpleCommand
{
	int _numberOfAvailableArguments;
	int _numberOfArguments;
	char **_arguments;

	SimpleCommand();
	void insertArgument(char *argument);
};

struct Command
{
	int _numberOfAvailableSimpleCommands;
	int _numberOfSimpleCommands;
	SimpleCommand **_simpleCommands;
	char *_outFile;
	char *_inputFile;
	char *_errFile;
	int  _out_error;
	int _background;
	int _append;

	void prompt();
	void print();
	void execute();
	void clear();

	Command();
	void insertSimpleCommand(SimpleCommand *simpleCommand);

	static Command _currentCommand;
	static SimpleCommand *_currentSimpleCommand;
};

#endif
