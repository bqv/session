#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

using namespace std;
namespace fs = filesystem;

class Tree {
  private:
    size_t dirs = 0;
    size_t files = 0;

    vector<string> inner_pointers = { "├── ", "│   " };
    vector<string> final_pointers = { "└── ", "    " };

  public:
    void walk(string directory, string prefix) {
      vector<fs::directory_entry> entries;

      try {
        for (const auto &entry : fs::directory_iterator(directory)) {
          if (entry.path().filename().string()[0] != '.') {
            entries.push_back(entry);
          }
        }
      }
      catch (const exception &ex) {
        cout << prefix << final_pointers[0] << "! " << ex.what() << endl;
      }

      sort(entries.begin(), entries.end(), [](const fs::directory_entry &left, const fs::directory_entry &right) -> bool {
        return left.path().filename() < right.path().filename();
      });

      for (size_t index = 0; index < entries.size(); index++) {
        fs::directory_entry entry = entries[index];
        vector<string> pointers = index == entries.size() - 1 ? final_pointers : inner_pointers;

        cout << prefix << pointers[0] << entry.path().filename().string();

        if (entry.is_regular_file()) {
          vector<string> lines;
          try {
            ifstream ifs{entry.path()};
            string line;
            while (lines.size() <= 5 && getline(ifs, line)) {
              lines.push_back(line);
            }
          }
          catch (const exception &ex) {
          }

          string header = accumulate(lines.begin(), lines.end(), string());
          for (char ch : header) {
            if (!(ch >= 0 && ch <= 127)) {
              lines.clear();
              lines.push_back("<binary>");
              break;
            }
          }

          switch (lines.size()) {
            case 0:
              cout << endl;
              break;
 
            case 1:
              cout << " = " << lines[0] << endl;
              break;
 
            default:
              cout << " ┏ " << lines[0] << endl;
              bool more = false;
              if (lines.size() > 5) {
                lines.resize(5);
                more = true;
              }
              lines.erase(lines.begin());
              auto indent = string(entry.path().filename().string().size(), ' ');
              for (const auto &line : lines)
                cout << prefix << pointers[1] << indent << " ┃ " << line << endl;
              cout << prefix << pointers[1] << indent << (more ? " ┇" : " ┗") << endl;
          }
        }
        else if (entry.is_fifo()) {
          cout << " = " << "<fifo>" << endl;
        }
        else if (entry.is_socket()) {
          cout << " = " << "<socket>" << endl;
        }
        else if (entry.is_symlink()) {
          cout << " -> " << fs::read_symlink(entry.path()) << endl;
        }
        else 
          cout << endl;

        if (!entry.is_directory()) {
          files++;
        } else {
          dirs++;
          walk(entry.path(), prefix + pointers[1]);
        }
      }
    }

    void summary() {
      cout << "\n" << dirs << " directories," << " " << files << " files" << endl;
    }
};

int main(int argc, char *argv[]) {
  Tree tree;
  string directory = argc == 1 ? "." : argv[1];

  cout << directory << endl;
  tree.walk(directory, "");
  tree.summary();

  return 0;
}
