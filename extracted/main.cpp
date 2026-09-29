#include "rip/archive.hpp"

#include <iostream>
#include <string_view>

namespace
{

  void print_usage()
  {
    std::cout << "RIP - Rip Archive Utility\n"
              << "\n"
              << "Usage:\n"
              << "  rip create <archive.rip> <file-or-directory>\n"
              << "  rip list    <archive.rip>\n"
              << "  rip test    <archive.rip>\n"
              << "  rip extract <archive.rip> <directory>\n";
  }

} // namespace

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    print_usage();
    return 1;
  }

  const std::string_view command = argv[1];

  if (command == "create")
  {
    if (argc != 4)
    {
      print_usage();
      return 1;
    }

    return rip::Archive::create(argv[2], argv[3]) ? 0 : 1;
  }

  if (command == "list")
  {
    if (argc != 3)
    {
      print_usage();
      return 1;
    }

    return rip::Archive::list(argv[2]) ? 0 : 1;
  }

  if (command == "test")
  {
    if (argc != 3)
    {
      print_usage();
      return 1;
    }

    return rip::Archive::test(argv[2]) ? 0 : 1;
  }

  if (command == "extract")
  {
    if (argc != 4)
    {
      print_usage();
      return 1;
    }

    return rip::Archive::extract(argv[2], argv[3]) ? 0 : 1;
  }

  if (command == "help" || command == "--help" || command == "-h")
  {
    print_usage();
    return 0;
  }

  std::cerr << "rip: unknown command: " << command << '\n';

  print_usage();

  return 1;
}