#include "app.h"
#include <csync/csync.h>

#include <QApplication>
#include <QPalette>

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);

  // Keep the interface readable when Windows is configured for dark mode.
  // The tree view delegates otherwise inherit a dark foreground on a dark
  // background from the system palette.
  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#f5f5f5"));
  palette.setColor(QPalette::WindowText, QColor("#202020"));
  palette.setColor(QPalette::Base, QColor("#ffffff"));
  palette.setColor(QPalette::AlternateBase, QColor("#f0f0f0"));
  palette.setColor(QPalette::Text, QColor("#202020"));
  palette.setColor(QPalette::Button, QColor("#e8e8e8"));
  palette.setColor(QPalette::ButtonText, QColor("#202020"));
  palette.setColor(QPalette::Highlight, QColor("#cce4ff"));
  palette.setColor(QPalette::HighlightedText, QColor("#202020"));
  a.setPalette(palette);

  App w("https://canvas.nus.edu.sg");
  w.show();
  return a.exec();
}
