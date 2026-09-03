/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 5.12.4
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralWidget;
    QLineEdit *backOrigin_velocity;
    QLabel *label_20;
    QLabel *label_32;
    QGroupBox *groupBox;
    QGridLayout *gridLayout_7;
    QLabel *label_2;
    QLabel *label_27;
    QLineEdit *IP_Edit;
    QLabel *label;
    QLineEdit *port_Edit;
    QPushButton *connectBtn;
    QPushButton *xSubBtn;
    QLineEdit *jog_velocity;
    QPushButton *backZeroScanBtn;
    QPushButton *yAddBtn;
    QPushButton *ySubBtn;
    QPushButton *xAddBtn;
    QPushButton *resetScanBtn;
    QLabel *label_13;
    QPushButton *setMOrigin;
    QLabel *label_33;
    QLabel *label_38;
    QLabel *label_39;
    QLabel *label_34;
    QLabel *label_50;
    QLineEdit *sweepSpeed;
    QLabel *label_21;
    QPushButton *startScanBtn;
    QPushButton *endScanBtn;
    QLabel *xCurPos;
    QLabel *yCurPos;
    QLineEdit *y_step;
    QLabel *label_18;
    QLineEdit *x_lenght;
    QLabel *label_17;
    QLabel *label_19;
    QLineEdit *y_lenght;
    QLabel *label_22;
    QLabel *label_23;
    QLabel *label_24;
    QLabel *label_51;
    QLabel *label_26;
    QComboBox *comboBox_3;
    QPushButton *stopScanBtn;
    QMenuBar *menuBar;
    QToolBar *mainToolBar;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QString::fromUtf8("MainWindow"));
        MainWindow->resize(1090, 598);
        centralWidget = new QWidget(MainWindow);
        centralWidget->setObjectName(QString::fromUtf8("centralWidget"));
        backOrigin_velocity = new QLineEdit(centralWidget);
        backOrigin_velocity->setObjectName(QString::fromUtf8("backOrigin_velocity"));
        backOrigin_velocity->setGeometry(QRect(140, 320, 161, 31));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(backOrigin_velocity->sizePolicy().hasHeightForWidth());
        backOrigin_velocity->setSizePolicy(sizePolicy);
        QFont font;
        font.setPointSize(15);
        backOrigin_velocity->setFont(font);
        label_20 = new QLabel(centralWidget);
        label_20->setObjectName(QString::fromUtf8("label_20"));
        label_20->setGeometry(QRect(10, 320, 131, 31));
        label_32 = new QLabel(centralWidget);
        label_32->setObjectName(QString::fromUtf8("label_32"));
        label_32->setGeometry(QRect(310, 390, 61, 31));
        groupBox = new QGroupBox(centralWidget);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        groupBox->setGeometry(QRect(-20, 0, 761, 101));
        groupBox->setFont(font);
        gridLayout_7 = new QGridLayout(groupBox);
        gridLayout_7->setSpacing(6);
        gridLayout_7->setContentsMargins(11, 11, 11, 11);
        gridLayout_7->setObjectName(QString::fromUtf8("gridLayout_7"));
        label_2 = new QLabel(groupBox);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setFont(font);
        label_2->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        gridLayout_7->addWidget(label_2, 0, 3, 1, 1);

        label_27 = new QLabel(groupBox);
        label_27->setObjectName(QString::fromUtf8("label_27"));

        gridLayout_7->addWidget(label_27, 1, 1, 1, 1);

        IP_Edit = new QLineEdit(groupBox);
        IP_Edit->setObjectName(QString::fromUtf8("IP_Edit"));
        sizePolicy.setHeightForWidth(IP_Edit->sizePolicy().hasHeightForWidth());
        IP_Edit->setSizePolicy(sizePolicy);
        IP_Edit->setFont(font);

        gridLayout_7->addWidget(IP_Edit, 0, 2, 1, 1);

        label = new QLabel(groupBox);
        label->setObjectName(QString::fromUtf8("label"));
        label->setFont(font);
        label->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);

        gridLayout_7->addWidget(label, 0, 1, 1, 1);

        port_Edit = new QLineEdit(groupBox);
        port_Edit->setObjectName(QString::fromUtf8("port_Edit"));
        sizePolicy.setHeightForWidth(port_Edit->sizePolicy().hasHeightForWidth());
        port_Edit->setSizePolicy(sizePolicy);
        port_Edit->setFont(font);

        gridLayout_7->addWidget(port_Edit, 0, 4, 1, 1);

        connectBtn = new QPushButton(groupBox);
        connectBtn->setObjectName(QString::fromUtf8("connectBtn"));

        gridLayout_7->addWidget(connectBtn, 0, 5, 1, 1);

        gridLayout_7->setColumnStretch(4, 1);
        gridLayout_7->setColumnStretch(5, 1);
        xSubBtn = new QPushButton(centralWidget);
        xSubBtn->setObjectName(QString::fromUtf8("xSubBtn"));
        xSubBtn->setGeometry(QRect(330, 170, 81, 51));
        QSizePolicy sizePolicy1(QSizePolicy::Minimum, QSizePolicy::Preferred);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(xSubBtn->sizePolicy().hasHeightForWidth());
        xSubBtn->setSizePolicy(sizePolicy1);
        xSubBtn->setAutoRepeat(false);
        xSubBtn->setAutoExclusive(false);
        jog_velocity = new QLineEdit(centralWidget);
        jog_velocity->setObjectName(QString::fromUtf8("jog_velocity"));
        jog_velocity->setGeometry(QRect(140, 390, 161, 32));
        sizePolicy.setHeightForWidth(jog_velocity->sizePolicy().hasHeightForWidth());
        jog_velocity->setSizePolicy(sizePolicy);
        jog_velocity->setFont(font);
        backZeroScanBtn = new QPushButton(centralWidget);
        backZeroScanBtn->setObjectName(QString::fromUtf8("backZeroScanBtn"));
        backZeroScanBtn->setGeometry(QRect(10, 480, 121, 51));
        yAddBtn = new QPushButton(centralWidget);
        yAddBtn->setObjectName(QString::fromUtf8("yAddBtn"));
        yAddBtn->setGeometry(QRect(420, 110, 81, 51));
        sizePolicy1.setHeightForWidth(yAddBtn->sizePolicy().hasHeightForWidth());
        yAddBtn->setSizePolicy(sizePolicy1);
        yAddBtn->setAutoRepeat(false);
        yAddBtn->setAutoExclusive(false);
        ySubBtn = new QPushButton(centralWidget);
        ySubBtn->setObjectName(QString::fromUtf8("ySubBtn"));
        ySubBtn->setGeometry(QRect(420, 230, 81, 51));
        sizePolicy1.setHeightForWidth(ySubBtn->sizePolicy().hasHeightForWidth());
        ySubBtn->setSizePolicy(sizePolicy1);
        ySubBtn->setAutoRepeat(false);
        ySubBtn->setAutoExclusive(false);
        xAddBtn = new QPushButton(centralWidget);
        xAddBtn->setObjectName(QString::fromUtf8("xAddBtn"));
        xAddBtn->setGeometry(QRect(510, 170, 81, 51));
        sizePolicy1.setHeightForWidth(xAddBtn->sizePolicy().hasHeightForWidth());
        xAddBtn->setSizePolicy(sizePolicy1);
        xAddBtn->setAutoRepeat(false);
        xAddBtn->setAutoExclusive(false);
        resetScanBtn = new QPushButton(centralWidget);
        resetScanBtn->setObjectName(QString::fromUtf8("resetScanBtn"));
        resetScanBtn->setGeometry(QRect(180, 480, 121, 51));
        label_13 = new QLabel(centralWidget);
        label_13->setObjectName(QString::fromUtf8("label_13"));
        label_13->setGeometry(QRect(10, 390, 131, 32));
        setMOrigin = new QPushButton(centralWidget);
        setMOrigin->setObjectName(QString::fromUtf8("setMOrigin"));
        setMOrigin->setGeometry(QRect(10, 250, 121, 51));
        sizePolicy1.setHeightForWidth(setMOrigin->sizePolicy().hasHeightForWidth());
        setMOrigin->setSizePolicy(sizePolicy1);
        label_33 = new QLabel(centralWidget);
        label_33->setObjectName(QString::fromUtf8("label_33"));
        label_33->setGeometry(QRect(10, 188, 81, 41));
        label_38 = new QLabel(centralWidget);
        label_38->setObjectName(QString::fromUtf8("label_38"));
        label_38->setGeometry(QRect(270, 188, 31, 41));
        label_39 = new QLabel(centralWidget);
        label_39->setObjectName(QString::fromUtf8("label_39"));
        label_39->setGeometry(QRect(270, 120, 31, 29));
        label_34 = new QLabel(centralWidget);
        label_34->setObjectName(QString::fromUtf8("label_34"));
        label_34->setGeometry(QRect(10, 120, 81, 29));
        label_50 = new QLabel(centralWidget);
        label_50->setObjectName(QString::fromUtf8("label_50"));
        label_50->setGeometry(QRect(310, 320, 61, 31));
        sweepSpeed = new QLineEdit(centralWidget);
        sweepSpeed->setObjectName(QString::fromUtf8("sweepSpeed"));
        sweepSpeed->setGeometry(QRect(980, 130, 101, 31));
        sizePolicy.setHeightForWidth(sweepSpeed->sizePolicy().hasHeightForWidth());
        sweepSpeed->setSizePolicy(sizePolicy);
        sweepSpeed->setFont(font);
        label_21 = new QLabel(centralWidget);
        label_21->setObjectName(QString::fromUtf8("label_21"));
        label_21->setGeometry(QRect(870, 130, 111, 31));
        startScanBtn = new QPushButton(centralWidget);
        startScanBtn->setObjectName(QString::fromUtf8("startScanBtn"));
        startScanBtn->setGeometry(QRect(380, 480, 121, 51));
        endScanBtn = new QPushButton(centralWidget);
        endScanBtn->setObjectName(QString::fromUtf8("endScanBtn"));
        endScanBtn->setGeometry(QRect(760, 480, 121, 51));
        xCurPos = new QLabel(centralWidget);
        xCurPos->setObjectName(QString::fromUtf8("xCurPos"));
        xCurPos->setGeometry(QRect(100, 130, 141, 21));
        yCurPos = new QLabel(centralWidget);
        yCurPos->setObjectName(QString::fromUtf8("yCurPos"));
        yCurPos->setGeometry(QRect(100, 200, 141, 21));
        y_step = new QLineEdit(centralWidget);
        y_step->setObjectName(QString::fromUtf8("y_step"));
        y_step->setGeometry(QRect(730, 272, 101, 31));
        sizePolicy.setHeightForWidth(y_step->sizePolicy().hasHeightForWidth());
        y_step->setSizePolicy(sizePolicy);
        y_step->setFont(font);
        label_18 = new QLabel(centralWidget);
        label_18->setObjectName(QString::fromUtf8("label_18"));
        label_18->setGeometry(QRect(839, 190, 20, 53));
        x_lenght = new QLineEdit(centralWidget);
        x_lenght->setObjectName(QString::fromUtf8("x_lenght"));
        x_lenght->setGeometry(QRect(730, 132, 101, 31));
        sizePolicy.setHeightForWidth(x_lenght->sizePolicy().hasHeightForWidth());
        x_lenght->setSizePolicy(sizePolicy);
        x_lenght->setFont(font);
        label_17 = new QLabel(centralWidget);
        label_17->setObjectName(QString::fromUtf8("label_17"));
        label_17->setGeometry(QRect(840, 120, 20, 53));
        label_19 = new QLabel(centralWidget);
        label_19->setObjectName(QString::fromUtf8("label_19"));
        label_19->setGeometry(QRect(840, 260, 20, 53));
        y_lenght = new QLineEdit(centralWidget);
        y_lenght->setObjectName(QString::fromUtf8("y_lenght"));
        y_lenght->setGeometry(QRect(730, 202, 101, 31));
        sizePolicy.setHeightForWidth(y_lenght->sizePolicy().hasHeightForWidth());
        y_lenght->setSizePolicy(sizePolicy);
        y_lenght->setFont(font);
        label_22 = new QLabel(centralWidget);
        label_22->setObjectName(QString::fromUtf8("label_22"));
        label_22->setGeometry(QRect(620, 130, 101, 31));
        label_23 = new QLabel(centralWidget);
        label_23->setObjectName(QString::fromUtf8("label_23"));
        label_23->setGeometry(QRect(620, 200, 101, 31));
        label_24 = new QLabel(centralWidget);
        label_24->setObjectName(QString::fromUtf8("label_24"));
        label_24->setGeometry(QRect(620, 270, 101, 31));
        label_51 = new QLabel(centralWidget);
        label_51->setObjectName(QString::fromUtf8("label_51"));
        label_51->setGeometry(QRect(1090, 130, 61, 31));
        label_26 = new QLabel(centralWidget);
        label_26->setObjectName(QString::fromUtf8("label_26"));
        label_26->setGeometry(QRect(870, 200, 101, 31));
        comboBox_3 = new QComboBox(centralWidget);
        comboBox_3->addItem(QString());
        comboBox_3->addItem(QString());
        comboBox_3->setObjectName(QString::fromUtf8("comboBox_3"));
        comboBox_3->setGeometry(QRect(980, 190, 101, 41));
        stopScanBtn = new QPushButton(centralWidget);
        stopScanBtn->setObjectName(QString::fromUtf8("stopScanBtn"));
        stopScanBtn->setGeometry(QRect(580, 480, 121, 51));
        MainWindow->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(MainWindow);
        menuBar->setObjectName(QString::fromUtf8("menuBar"));
        menuBar->setGeometry(QRect(0, 0, 1090, 21));
        MainWindow->setMenuBar(menuBar);
        mainToolBar = new QToolBar(MainWindow);
        mainToolBar->setObjectName(QString::fromUtf8("mainToolBar"));
        MainWindow->addToolBar(Qt::TopToolBarArea, mainToolBar);
        statusBar = new QStatusBar(MainWindow);
        statusBar->setObjectName(QString::fromUtf8("statusBar"));
        MainWindow->setStatusBar(statusBar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QApplication::translate("MainWindow", "MainWindow", nullptr));
        label_20->setText(QApplication::translate("MainWindow", "\345\233\236\345\216\237\347\202\271\351\200\237\345\272\246\357\274\232", nullptr));
        label_32->setText(QApplication::translate("MainWindow", "mm/s", nullptr));
        groupBox->setTitle(QString());
        label_2->setText(QApplication::translate("MainWindow", "Port:", nullptr));
        label_27->setText(QString());
        IP_Edit->setText(QApplication::translate("MainWindow", "192.168.1.13", nullptr));
        label->setText(QApplication::translate("MainWindow", "IP1:", nullptr));
        port_Edit->setText(QApplication::translate("MainWindow", "8802", nullptr));
        connectBtn->setText(QApplication::translate("MainWindow", "connect", nullptr));
        xSubBtn->setText(QApplication::translate("MainWindow", "\345\267\246\350\275\254", nullptr));
        backZeroScanBtn->setText(QApplication::translate("MainWindow", "\345\233\236\345\216\237", nullptr));
        yAddBtn->setText(QApplication::translate("MainWindow", "\345\211\215\350\277\233", nullptr));
        ySubBtn->setText(QApplication::translate("MainWindow", "\345\220\216\351\200\200", nullptr));
        xAddBtn->setText(QApplication::translate("MainWindow", "\345\217\263\350\275\254", nullptr));
        resetScanBtn->setText(QApplication::translate("MainWindow", "\345\244\215\344\275\215", nullptr));
        label_13->setText(QApplication::translate("MainWindow", "\347\202\271\345\212\250\351\200\237\345\272\246\357\274\232", nullptr));
        setMOrigin->setText(QApplication::translate("MainWindow", "\350\256\276\347\275\256\351\233\266\347\202\271", nullptr));
        label_33->setText(QApplication::translate("MainWindow", "\351\251\261\350\275\256:", nullptr));
        label_38->setText(QApplication::translate("MainWindow", "mm", nullptr));
        label_39->setText(QApplication::translate("MainWindow", "mm", nullptr));
        label_34->setText(QApplication::translate("MainWindow", "\350\275\254\350\275\256:", nullptr));
        label_50->setText(QApplication::translate("MainWindow", "mm/s", nullptr));
        label_21->setText(QApplication::translate("MainWindow", "\346\211\253\346\237\245\351\200\237\345\272\246\357\274\232", nullptr));
        startScanBtn->setText(QApplication::translate("MainWindow", "\345\274\200\345\247\213", nullptr));
        endScanBtn->setText(QApplication::translate("MainWindow", "\347\273\223\346\235\237", nullptr));
        xCurPos->setText(QString());
        yCurPos->setText(QString());
        label_18->setText(QApplication::translate("MainWindow", "mm", nullptr));
        label_17->setText(QApplication::translate("MainWindow", "mm", nullptr));
        label_19->setText(QApplication::translate("MainWindow", "mm", nullptr));
        label_22->setText(QApplication::translate("MainWindow", "\346\211\253\346\237\245\350\275\264\351\225\277\357\274\232", nullptr));
        label_23->setText(QApplication::translate("MainWindow", "\346\255\245\350\277\233\350\275\264\351\225\277\357\274\232", nullptr));
        label_24->setText(QApplication::translate("MainWindow", "\346\255\245\350\277\233\351\225\277\345\272\246\357\274\232", nullptr));
        label_51->setText(QApplication::translate("MainWindow", "mm/s", nullptr));
        label_26->setText(QApplication::translate("MainWindow", "\346\211\253\346\237\245\346\250\241\345\274\217\357\274\232", nullptr));
        comboBox_3->setItemText(0, QApplication::translate("MainWindow", "\345\274\223\345\275\242", nullptr));
        comboBox_3->setItemText(1, QApplication::translate("MainWindow", "\347\216\257\345\275\242", nullptr));

        stopScanBtn->setText(QApplication::translate("MainWindow", "\345\274\200\345\247\213", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
