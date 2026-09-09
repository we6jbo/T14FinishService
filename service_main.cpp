#include <QCoreApplication>
#include "finishservice.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("T14FinishService");
    QCoreApplication::setApplicationVersion("0.2");

    FinishService service;
    if (!service.start())
        return 1;

    return app.exec();
}
