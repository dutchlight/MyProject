#include "jnsolar.h"
#include "ui_jnsolar.h"
#include <QMessageBox>
#include <QDesktopWidget>
#include <QModelIndex>
#include <QScrollBar>

#define BTDataServiceUUID "0000ffe0-0000-1000-8000-00805F9B34FB"   //蓝牙输入转发到串口输出


JNSolar::JNSolar(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::JNSolar)
{
    ui->setupUi(this);
    m_screenRect = QApplication::desktop()->screenGeometry();
    setFixedSize(m_screenRect.width(), m_screenRect.height()); //固定窗口大小

    //m_pLoginDlg = new LoginDlg(this);
    //m_pLoginDlg->show();

    LoginDlg lgdlg(this);
    lgdlg.exec();

    m_pDevInfo = NULL;
    m_pControl = NULL;
    m_pReadService = NULL;
    m_pWriteService = NULL;

    m_foundReadService = false;
    m_foundWriteService = false;

    m_bDcacComm = false;
    m_nDcacLostMsgCnt = MAX_DCAC_COMM_LOST_NUM + 1;//default set lost msg status
    m_nCurRecvLen = 0;
    memset(m_acData, 0, 1024);
    memset((U8 *)&m_stDcacReg, 0, sizeof(m_stDcacReg));
    memset((U8 *)&m_stDcacWrReg, 0, sizeof(m_stDcacWrReg));
    memset((U8 *)&m_stDcacLastTxMsg, 0, sizeof(m_stDcacLastTxMsg));
    m_queueMsg06.clear();
    m_queueMsg03.clear();

    //ui->label_tip->setText(QString::number(m_screenRect.width()) + "*" + QString::number(m_screenRect.height()));
    int tabCount = ui->tabWidget->count();
    int tabWidth = m_screenRect.width()/tabCount;// - m_screenRect.width()/50;
    int tabHeight = m_screenRect.height()/11;
    QString strTabWgt;
    strTabWgt = "QTabWidget::pane{border-image: url(:/imgs/bk_info.jpg);}\
            QTabBar::tab:hover{\
            background:rgb(255, 170, 0, 100);\
           }\
           QTabBar::tab:selected{\
               border-color:rgb(255, 200, 0);\
               background:rgb(255, 200, 0);\
               color:rgb(0, 85, 255);\
           }\
           QTabBar::tab:last {\
           margin-top: 0px; margin-left: 0; margin-right: 0;}";
    strTabWgt += QString("QTabBar::tab{width:%1px;height:%2px;\
                        background-color: rgb(180, 255, 180);\
                        color:rgb(0, 128, 255);}").arg(tabWidth).arg(tabHeight);
    ui->tabWidget->setStyleSheet(strTabWgt);//border-image: url(:/imgs/bk_info.jpg);

    listTableStatusInit();
    listTableSetInit();

    //[devicediscovery-1]
    m_pDevDiscoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_pDevDiscoveryAgent->setLowEnergyDiscoveryTimeout(5000);

    connect(m_pDevDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &JNSolar::addDevice);
    connect(m_pDevDiscoveryAgent, static_cast<void (QBluetoothDeviceDiscoveryAgent::*)(QBluetoothDeviceDiscoveryAgent::Error)>(&QBluetoothDeviceDiscoveryAgent::error),
            this, &JNSolar::scanError);
    connect(m_pDevDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, &JNSolar::scanFinished);
    connect(m_pDevDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::canceled, this, &JNSolar::scanFinished);
    //[devicediscovery-1]

    m_pTimSend = new QTimer(this);
    connect(m_pTimSend, SIGNAL(timeout()), this, SLOT(dcacSendTimeout()));
    m_pTimSend->stop();

    m_pTimRecv = new QTimer(this);
    connect(m_pTimRecv, SIGNAL(timeout()), this, SLOT(dcacRecvJudge()));
    m_pTimRecv->stop();

    QTimer *pTimUpdate = new QTimer(this);
    connect(pTimUpdate, SIGNAL(timeout()), this, SLOT(updateLabelData()));
    pTimUpdate->start(1000);
}

JNSolar::~JNSolar()
{
    delete ui;
}

void JNSolar::on_btnScan_clicked()
{
    ui->listDevs->clear();
    m_listDevinfo.clear();
    m_pDevInfo = 0;
    ui->btnScan->setDisabled(true);

    //[devicediscovery-2]
    m_pDevDiscoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    //[devicediscovery-2]

}

void JNSolar::addDevice(const QBluetoothDeviceInfo &device)
{
    // If device is LowEnergy-device, add it to the list
    if (device.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
    {
        m_listDevinfo.append(device);
        QListWidgetItem * pItem = new QListWidgetItem;
        pItem->setSizeHint(QSize(m_screenRect.width(),m_screenRect.height()/15));
        pItem->setText(device.address().toString() + "  " + device.name());
        ui->listDevs->addItem(pItem);
        ui->label_tip->setText(tr("Low energy device found. Scanning more..."));
    }

}

void JNSolar::scanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    if (error == QBluetoothDeviceDiscoveryAgent::PoweredOffError)
    {
        QMessageBox::information(this,"error",tr("The Bluetooth adaptor is powered off."),QMessageBox::Ok);
    }
    else if (error == QBluetoothDeviceDiscoveryAgent::InputOutputError)
    {
        QMessageBox::information(this,"error",tr("Writing or reading from the device resulted in an error."),QMessageBox::Ok);
    }
    else
    {
        QMessageBox::information(this,"error",tr("An unknown error has occurred."),QMessageBox::Ok);
    }

    ui->btnScan->setEnabled(true);
}

void JNSolar::scanFinished()
{
    ui->btnScan->setEnabled(true);

    ui->label_tip->setText(tr("Scan finished"));
}

void JNSolar::on_listDevs_itemDoubleClicked(QListWidgetItem *item)
{
    m_pDevDiscoveryAgent->stop();

    int nCurIdx = ui->listDevs->currentRow();
    if(m_listDevinfo.count() > nCurIdx)
    {
        m_pDevInfo = (QBluetoothDeviceInfo *)&m_listDevinfo.at(nCurIdx);
        ui->label_tip->setText("select " + m_pDevInfo->name());
    }
    else
    {
        ui->label_tip->setText(tr("no found devinfo index."));
        return;
    }

    // Disconnect and delete old connection
    if (m_pControl)
    {
        m_pControl->disconnectFromDevice();
        delete m_pControl;
        m_pControl = 0;
    }

    // Create new controller and connect it if device available
    if (m_pDevInfo)
    {
        // Make connections
        //[Connect-Signals-1]
        m_pControl = new QLowEnergyController(*m_pDevInfo, this);
        // [Connect-Signals-1]
        m_pControl->setRemoteAddressType(QLowEnergyController::PublicAddress);
        // [Connect-Signals-2]
        connect(m_pControl, &QLowEnergyController::serviceDiscovered,this, &JNSolar::serviceDiscovered);
        connect(m_pControl, &QLowEnergyController::discoveryFinished,this, &JNSolar::serviceScanDone);

        connect(m_pControl, static_cast<void (QLowEnergyController::*)(QLowEnergyController::Error)>(&QLowEnergyController::error),
                this, [this](QLowEnergyController::Error error) {
            Q_UNUSED(error);
            ui->label_tip->setText("Cannot connect to device.");
        });
        connect(m_pControl, &QLowEnergyController::connected, this, [this]() {
            ui->label_tip->setText("bt controller connected. Search services...");
            m_pControl->discoverServices();
        });
        connect(m_pControl, &QLowEnergyController::disconnected, this, [this]() {
            ui->label_tip->setText("bt controller disconnected");
        });

        // Connect
        m_pControl->connectToDevice();
        // [Connect-Signals-2]
    }
}

//! [Filter service 1]
void JNSolar::serviceDiscovered(const QBluetoothUuid &gatt)
{
    if (gatt == QBluetoothUuid(QBluetoothUuid(QUuid("0000ffe0-0000-1000-8000-00805F9B34FB"))))
    {
        ui->label_tip->setText("bt data service discovered. Waiting for service scan to be done...");
        m_foundReadService = true;
    }
    else if(gatt == QBluetoothUuid(QBluetoothUuid(QUuid("0000ffe5-0000-1000-8000-00805F9B34FB"))))
    {
        ui->label_tip->setText("bt write service discovered. Waiting for service scan to be done...");
        m_foundWriteService = true;
    }

}
//! [Filter service 1]

void JNSolar::serviceScanDone()
{
    ui->label_tip->setText("Service scan done.");

    // Delete old service if available
    if (m_pReadService)
    {
        delete m_pReadService;
        m_pReadService = 0;
    }
    if (m_pWriteService)
    {
        delete m_pWriteService;
        m_pWriteService = 0;
    }

// [Filter service 2]
    // If data Service found, create new service
    if (m_foundReadService)
    {
        m_pReadService = m_pControl->createServiceObject(QBluetoothUuid(QUuid("0000ffe0-0000-1000-8000-00805F9B34FB")), this);
    }
    if (m_pReadService)
    {
        connect(m_pReadService, &QLowEnergyService::stateChanged, this, &JNSolar::readServiceStateChanged);
        connect(m_pReadService, &QLowEnergyService::characteristicChanged, this, &JNSolar::readData);
        connect(m_pReadService, &QLowEnergyService::descriptorWritten, this, &JNSolar::confirmedDescriptorWrite);
        m_pReadService->discoverDetails();
    }
    else
    {
        ui->label_tip->setText("bt data service not found.");
    }
    // If serial Service found, create new service
    if (m_foundWriteService)
    {
        m_pWriteService = m_pControl->createServiceObject(QBluetoothUuid(QUuid("0000ffe5-0000-1000-8000-00805F9B34FB")), this);
        connect(m_pWriteService, &QLowEnergyService::stateChanged, this, &JNSolar::writeServiceStateChanged);
        m_pWriteService->discoverDetails();
    }
    else
    {
        ui->label_tip->setText("bt write service not found.");
    }
 // [Filter service 2]
}

// Service functions
//! [Find read characteristic]
void JNSolar::readServiceStateChanged(QLowEnergyService::ServiceState s)
{
    switch (s)
    {
    case QLowEnergyService::DiscoveringServices:
        ui->label_tip->setText(tr("read discovering services..."));
        break;
    case QLowEnergyService::ServiceDiscovered:
    {
        ui->label_tip->setText(tr("read service discovered."));

        const QLowEnergyCharacteristic hrChar = m_pReadService->characteristic(QBluetoothUuid(QUuid("0000ffe4-0000-1000-8000-00805F9B34FB")));
        if (!hrChar.isValid())
        {
            ui->label_tip->setText("read service not found.");
        }
        else
        {
            m_stReadDesc = hrChar.descriptor(QBluetoothUuid::ClientCharacteristicConfiguration);
            if (m_stReadDesc.isValid())
            {
                m_pReadService->writeDescriptor(m_stReadDesc, QByteArray::fromHex("0100"));
            }
            ui->tabWidget->setCurrentWidget(ui->tabStatus);
        }
        break;
    }
    default:
        //nothing for now
        break;
    }
}
//! [Find read characteristic]

void JNSolar::writeServiceStateChanged(QLowEnergyService::ServiceState s)
{
    switch (s)
    {
        case QLowEnergyService::DiscoveringServices:
            ui->label_tip->setText(tr("wr discovering services..."));
            break;
        case QLowEnergyService::ServiceDiscovered:
        {
            ui->label_tip->setText(tr("wr service discovered."));

            m_btWrChar = m_pWriteService->characteristic(QBluetoothUuid(QUuid("0000ffe9-0000-1000-8000-00805F9B34FB")));
            if (!m_btWrChar.isValid())
            {
                ui->label_tip->setText("bt wr data not found.");
            }
            else
            {
                m_pWriteService->writeCharacteristic(m_btWrChar, QByteArray("jntech",6));
                ui->tabWidget->setCurrentWidget(ui->tabStatus);
                m_pTimSend->start(TIM_DCAC_COMM_VALUE);
                m_pTimRecv->start(50);
            }
            break;
        }
        default:
            //nothing for now
            break;
    }
}


//! [Reading value]
void JNSolar::readData(const QLowEnergyCharacteristic &c, const QByteArray &value)
{
    // ignore any other characteristic change -> shouldn't really happen though
    if (c.uuid() != QBluetoothUuid(QUuid("0000ffe4-0000-1000-8000-00805F9B34FB")))
        return;
    //ui->label_recv->setText("Recv:\r\n" + QString(value));
    dcacRead(value);
}
//! [Reading value]

void JNSolar::confirmedDescriptorWrite(const QLowEnergyDescriptor &d, const QByteArray &value)
{
    if (d.isValid() && d == m_stReadDesc && value == QByteArray::fromHex("0000"))
    {
        //disabled notifications -> assume disconnect intent
        m_pControl->disconnectFromDevice();
        delete m_pReadService;
        m_pReadService = 0;
    }
}

void JNSolar::disconnectService()
{
    m_foundReadService = false;
    m_foundWriteService = false;
    m_pTimSend->stop();
    m_pTimRecv->stop();

    m_bDcacComm = false;
    m_nDcacLostMsgCnt = MAX_DCAC_COMM_LOST_NUM + 1;

    //disable notifications
    if (m_stReadDesc.isValid() && m_pReadService
            && m_stReadDesc.value() == QByteArray::fromHex("0100"))
    {
        m_pReadService->writeDescriptor(m_stReadDesc, QByteArray::fromHex("0000"));
    }
    else
    {
        if (m_pControl)
        {
            m_pControl->disconnectFromDevice();
        }
        delete m_pReadService;
        m_pReadService = 0;
    }
}

void JNSolar::on_btnDisConn_clicked()
{
    disconnectService();
}

void JNSolar::listTableStatusInit()
{
    ui->tabwgtDcacSta->setColumnCount(3);    //3列
    ui->tabwgtDcacSta->setRowCount(25);      //25行

    QStringList tabHeader;
    tabHeader<<tr("序号")<<tr("名称")<<tr("逆变器值");
    //设置表头标签
    ui->tabwgtDcacSta->setHorizontalHeaderLabels(tabHeader);
    //设置垂直表头可见
    ui->tabwgtDcacSta->verticalHeader()->setVisible(false);
    //设置不可编辑
    ui->tabwgtDcacSta->setEditTriggers(QAbstractItemView::NoEditTriggers);
    //设置最后行列充满表宽度
    ui->tabwgtDcacSta->horizontalHeader()->setStretchLastSection(true);
    ui->tabwgtDcacSta->verticalHeader()->setStretchLastSection(true);
    //设置无边框
    ui->tabwgtDcacSta->setFrameShape(QFrame::NoFrame);
    //设置表头第x列的宽度
    ui->tabwgtDcacSta->horizontalHeader()->resizeSection(0,m_screenRect.width()/6);
    ui->tabwgtDcacSta->horizontalHeader()->resizeSection(1,m_screenRect.width()/5*2);
    //设置表头的高度
    ui->tabwgtDcacSta->horizontalHeader()->setFixedHeight(m_screenRect.height()/20);
    //设置表头字体颜色
    //QFont font = ui->tabwgtDcacSta->horizontalHeader()->font();
    //font.setPointSize(15);
    //ui->tabwgtDcacSta->horizontalHeader()->setFont(font);
    ui->tabwgtDcacSta->horizontalHeaderItem(0)->setTextColor(QColor(0,0,200));
    ui->tabwgtDcacSta->horizontalHeaderItem(1)->setTextColor(QColor(0,0,200));
    ui->tabwgtDcacSta->horizontalHeaderItem(2)->setTextColor(QColor(0,0,200));
    //设置行高
    ui->tabwgtDcacSta->verticalHeader()->setDefaultSectionSize(m_screenRect.height()/20);
    //设置选中背景色
    //ui->tabwgtDcacSta->setStyleSheet("selection-background-color:rgb(34, 175, 75);");
    //设置表头背景色
    ui->tabwgtDcacSta->horizontalHeader()->setStyleSheet("QHeaderView::section{background:skyblue;}");
    //设置选择行为时每次选择一行
    ui->tabwgtDcacSta->setSelectionBehavior(QAbstractItemView::SelectRows);
    //设置只能选择一行，不能多行选中
    ui->tabwgtDcacSta->setSelectionMode(QAbstractItemView::SingleSelection);
    //高亮禁止
    ui->tabwgtDcacSta->horizontalHeader()->setHighlightSections(false);
    //设置不显示格子线
    //ui->tabwgtDcacSta->setShowGrid(false);

    QString strSta=" QScrollBar:vertical\
    {background:rgba(0,0,0,0%);}\
    QScrollBar::handle:vertical\
    {background:rgba(135,206,203,75%);}\
    QScrollBar::handle:vertical:hover\
    {background:rgba(135,206,203,100%);}";
    ui->tabwgtDcacSta->verticalScrollBar()->setStyleSheet(strSta);

    ui->tabwgtDcacSta->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QString strNum;
    for(int i=0; i<25; i++)
    {
        strNum.sprintf("%d",i+1);
        ui->tabwgtDcacSta->setItem(i, 0, new QTableWidgetItem(strNum));
        ui->tabwgtDcacSta->setItem(i, 1, new QTableWidgetItem("-"));
        ui->tabwgtDcacSta->setItem(i, 2, new QTableWidgetItem("-"));
        ui->tabwgtDcacSta->item(i,0)->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
        ui->tabwgtDcacSta->item(i,2)->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
    }
    //列表项与更新值顺序需保持一致
    ui->tabwgtDcacSta->item(0,1)->setText(("蓄电池电压"));
    ui->tabwgtDcacSta->item(1,1)->setText(("光伏阵列电压"));
    ui->tabwgtDcacSta->item(2,1)->setText(("光伏充电电流"));
    ui->tabwgtDcacSta->item(3,1)->setText(("交流充电电流"));
    ui->tabwgtDcacSta->item(4,1)->setText(("蓄电池功率"));
    ui->tabwgtDcacSta->item(5,1)->setText(("光伏温度"));
    ui->tabwgtDcacSta->item(6,1)->setText(("故障码1"));
    ui->tabwgtDcacSta->item(7,1)->setText(("充电模式"));
    ui->tabwgtDcacSta->item(8,1)->setText("光伏充电运行状态");
    ui->tabwgtDcacSta->item(9,1)->setText("交流充电运行状态");
    ui->tabwgtDcacSta->item(10,1)->setText(("交流温度"));
    ui->tabwgtDcacSta->item(11,1)->setText(("SN"));
    ui->tabwgtDcacSta->item(12,1)->setText(("故障码3"));
    ui->tabwgtDcacSta->item(13,1)->setText(("逆变电压"));
    ui->tabwgtDcacSta->item(14,1)->setText(("逆变电流"));
    ui->tabwgtDcacSta->item(15,1)->setText(("交流输入电压"));
    ui->tabwgtDcacSta->item(16,1)->setText(("逆变功率"));
    ui->tabwgtDcacSta->item(17,1)->setText(("逆变频率"));
    ui->tabwgtDcacSta->item(18,1)->setText(("逆变器温度"));
    ui->tabwgtDcacSta->item(19,1)->setText(("故障码2"));
    ui->tabwgtDcacSta->item(20,1)->setText(("逆变模式"));
    ui->tabwgtDcacSta->item(21,1)->setText(("逆变状态"));
    ui->tabwgtDcacSta->item(22,1)->setText(("旁路状态"));
    ui->tabwgtDcacSta->item(23,1)->setText(("光伏累计产能"));
    ui->tabwgtDcacSta->item(24,1)->setText(("逆变累计产能"));

    //显示指定行
    //ui->tabwgtDcacSta->verticalScrollBar()->setSliderPosition(0);
}

void JNSolar::updateLabelData()
{
    QString strData;
    float fData=0;

    if(m_bDcacComm)
    {
        fData = m_stDcacReg.usBatVolt * (float)0.1;
        strData.sprintf("%.1fV",fData);
        ui->tabwgtDcacSta->item(0,2)->setText(strData);
        fData = m_stDcacReg.usPvArrayVolt * (float)0.1;
        strData.sprintf("%.1fV",fData);
        ui->tabwgtDcacSta->item(1,2)->setText(strData);
        fData = m_stDcacReg.usPvChargeCurr * (float)0.1;
        strData.sprintf("%.1fA",fData);
        ui->tabwgtDcacSta->item(2,2)->setText(strData);
        fData = m_stDcacReg.usAcChargeCurr * (float)0.1;
        strData.sprintf("%.1fA",fData);
        ui->tabwgtDcacSta->item(3,2)->setText(strData);
        strData.sprintf("%dW",m_stDcacReg.usBatPower);
        ui->tabwgtDcacSta->item(4,2)->setText(strData);
        strData.sprintf("%d℃",m_stDcacReg.usPvTemp);
        ui->tabwgtDcacSta->item(5,2)->setText(strData);
        strData.sprintf("0x%04x",m_stDcacReg.unFaultCode1.usFaultCode1);
        ui->tabwgtDcacSta->item(6,2)->setText(strData);
        switch(m_stDcacReg.usChargeMode)
        {
        case 0:
            strData = tr("自动");
            break;
        case 1:
            strData = tr("光伏");
            break;
        case 2:
            strData = tr("电网");
            break;
        default:
            strData.sprintf("unkown:%d",m_stDcacReg.usChargeMode);
            break;
        }
        ui->tabwgtDcacSta->item(7,2)->setText(strData);
        if(0 == m_stDcacReg.usPvChargeRunSta)
        {
           strData = tr("停止");
        }
        else if(1 == m_stDcacReg.usPvChargeRunSta)
        {
           strData = tr("运行");
        }
        else
        {
            strData = tr("未知");
        }
        ui->tabwgtDcacSta->item(8,2)->setText(strData);
        if(0 == m_stDcacReg.usAcChargeRunSta)
        {
           strData = tr("停止");
        }
        else if(1 == m_stDcacReg.usAcChargeRunSta)
        {
           strData = tr("运行");
        }
        else
        {
            strData = tr("未知");
        }
        ui->tabwgtDcacSta->item(9,2)->setText(strData);
        strData.sprintf("%d℃",m_stDcacReg.usAcTemp);
        ui->tabwgtDcacSta->item(10,2)->setText(strData);
        strData.sprintf("%04x%04x",m_stDcacReg.usSNHigh,\
                        m_stDcacReg.usSNLow);
        ui->tabwgtDcacSta->item(11,2)->setText(strData);
        strData.sprintf("0x%04x",m_stDcacReg.unFaultCode3.usFaultCode3);
        ui->tabwgtDcacSta->item(12,2)->setText(strData);
        strData.sprintf("%dV",m_stDcacReg.usInvertVolt);
        ui->tabwgtDcacSta->item(13,2)->setText(strData);
        fData = m_stDcacReg.usInvertCurr * (float)0.1;
        strData.sprintf("%.1fA",fData);
        ui->tabwgtDcacSta->item(14,2)->setText(strData);
        strData.sprintf("%dV",m_stDcacReg.usAcInVolt);
        ui->tabwgtDcacSta->item(15,2)->setText(strData);
        strData.sprintf("%dW",m_stDcacReg.usInvertPower);
        ui->tabwgtDcacSta->item(16,2)->setText(strData);
        fData = m_stDcacReg.usInvertFreq * (float)0.1;
        strData.sprintf("%.1fHz",fData);
        ui->tabwgtDcacSta->item(17,2)->setText(strData);
        strData.sprintf("%d℃",m_stDcacReg.usInverterTemp);
        ui->tabwgtDcacSta->item(18,2)->setText(strData);
        strData.sprintf("0x%04x",m_stDcacReg.unFaultCode2.usFaultCode2);
        ui->tabwgtDcacSta->item(19,2)->setText(strData);
        switch(m_stDcacReg.usInvertMode)
        {
        case 0:
            strData = tr("自动");
            break;
        case 1:
            strData = tr("光伏");
            break;
        case 2:
            strData = tr("电网");
            break;
        default:
            strData.sprintf("unkown:%d",m_stDcacReg.usInvertMode);
            break;
        }
        ui->tabwgtDcacSta->item(20,2)->setText(strData);
        if(0 == m_stDcacReg.usInvertSta)
        {
           strData = tr("停止");
        }
        else if(1 == m_stDcacReg.usInvertSta)
        {
           strData = tr("运行");
        }
        else
        {
            strData = tr("未知");
        }
        ui->tabwgtDcacSta->item(21,2)->setText(strData);
        if(0 == m_stDcacReg.usBypassSta)
        {
           strData = tr("停止");
        }
        else if(1 == m_stDcacReg.usBypassSta)
        {
           strData = tr("运行");
        }
        else
        {
            strData = tr("未知");
        }
        ui->tabwgtDcacSta->item(22,2)->setText(strData);
        fData = ((m_stDcacReg.usPvTotalOutHigh << 16) + \
                m_stDcacReg.usPvTotalOutLow) * (float)0.1;
        strData.sprintf("%.1fkWh",fData);
        ui->tabwgtDcacSta->item(23,2)->setText(strData);
        fData = ((m_stDcacReg.usInvertTotalOutHigh << 16) + \
                m_stDcacReg.usInvertTotalOutLow) * (float)0.1;
        strData.sprintf("%.1fkWh",fData);
        ui->tabwgtDcacSta->item(24,2)->setText(strData);
    }
    else
    {
        for(int k=0; k<25; k++)
        {
            ui->tabwgtDcacSta->item(k,2)->setText("-");
        }
    }

    updateSetData();
}

void JNSolar::listTableSetInit()
{
    ui->tableWgtSet->setColumnCount(5);    //5列
    ui->tableWgtSet->setRowCount(10);      //10行

    QStringList tabHeader;
    tabHeader<<tr("序号")<<tr("名称")<<tr("值")<<tr("查询")<<tr("设置");
    //设置表头标签
    ui->tableWgtSet->setHorizontalHeaderLabels(tabHeader);
    //设置垂直表头可见
    ui->tableWgtSet->verticalHeader()->setVisible(false);
    //设置不可编辑
    //ui->tableWgtSet->setEditTriggers(QAbstractItemView::NoEditTriggers);
    //设置最后行列充满表宽度
    ui->tableWgtSet->horizontalHeader()->setStretchLastSection(true);
    //ui->tableWgtSet->verticalHeader()->setStretchLastSection(true);
    //设置无边框
    ui->tableWgtSet->setFrameShape(QFrame::NoFrame);
    //设置表头第x列的宽度
    ui->tableWgtSet->horizontalHeader()->resizeSection(0,m_screenRect.width()/10);
    ui->tableWgtSet->horizontalHeader()->resizeSection(1,m_screenRect.width()/4);
    ui->tableWgtSet->horizontalHeader()->resizeSection(2,m_screenRect.width()/5);
    ui->tableWgtSet->horizontalHeader()->resizeSection(3,m_screenRect.width()/5);
    //设置表头的高度
    ui->tableWgtSet->horizontalHeader()->setFixedHeight(m_screenRect.height()/20);
    //设置表头字体颜色
    //QFont font = ui->tableWgtSet->horizontalHeader()->font();
    //font.setPointSize(15);
    //ui->tableWgtSet->horizontalHeader()->setFont(font);
    ui->tableWgtSet->horizontalHeaderItem(0)->setTextColor(QColor(0,0,200));
    ui->tableWgtSet->horizontalHeaderItem(1)->setTextColor(QColor(0,0,200));
    ui->tableWgtSet->horizontalHeaderItem(2)->setTextColor(QColor(0,0,200));
    ui->tableWgtSet->horizontalHeaderItem(3)->setTextColor(QColor(0,0,200));
    ui->tableWgtSet->horizontalHeaderItem(4)->setTextColor(QColor(0,0,200));
    //设置行高
    ui->tableWgtSet->verticalHeader()->setDefaultSectionSize(m_screenRect.height()/20);
    //设置选中背景色
    //ui->tableWgtSet->setStyleSheet("selection-background-color:rgb(34, 175, 75);");
    //设置表头背景色
    ui->tableWgtSet->horizontalHeader()->setStyleSheet("QHeaderView::section{background:skyblue;}");
    //设置选择行为时每次选择一格
    ui->tableWgtSet->setSelectionBehavior(QAbstractItemView::SelectItems);
    //设置只能选择一行，不能多行选中
    ui->tableWgtSet->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->tableWgtSet->horizontalHeader()->setHighlightSections(false);
    //设置不显示格子线
    //ui->tableWgtSet->setShowGrid(false);

    QString strSet=" QScrollBar:vertical\
    {background:rgba(0,0,0,0%);}\
    QScrollBar::handle:vertical\
    {background:rgba(100,200,0,50%);}\
    QScrollBar::handle:vertical:hover\
    {background:rgba(100,200,0,75%);}";
    ui->tableWgtSet->verticalScrollBar()->setStyleSheet(strSet);

    ui->tableWgtSet->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QString strNum;
    for(int i=0; i<10; i++)
    {
        strNum.sprintf("%d",i+1);
        ui->tableWgtSet->setItem(i, 0, new QTableWidgetItem(strNum));
        ui->tableWgtSet->setItem(i, 1, new QTableWidgetItem("-"));
        ui->tableWgtSet->setItem(i, 2, new QTableWidgetItem());
        ui->tableWgtSet->setItem(i, 3, new QTableWidgetItem());
        ui->tableWgtSet->setItem(i, 4, new QTableWidgetItem());
        ui->tableWgtSet->item(i,0)->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
        ui->tableWgtSet->item(i,2)->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
        ui->tableWgtSet->item(i,3)->setTextAlignment(Qt::AlignLeft|Qt::AlignTop);
        ui->tableWgtSet->item(i,4)->setTextAlignment(Qt::AlignLeft|Qt::AlignTop);

        ui->tableWgtSet->item(i,0)->setFlags(ui->tableWgtSet->item(i,0)->flags() & (~Qt::ItemIsEditable));
        ui->tableWgtSet->item(i,1)->setFlags(ui->tableWgtSet->item(i,1)->flags() & (~Qt::ItemIsEditable));

        QPushButton *pQueryBtn = new QPushButton(this);
        pQueryBtn->setFocusPolicy(Qt::NoFocus);
        pQueryBtn->setStyleSheet("text-align:top;");
        ui->tableWgtSet->setCellWidget(i, 3, pQueryBtn);
        connect(pQueryBtn, SIGNAL(clicked()), this, SLOT(queryBtnClicked()));

        QPushButton *pSetBtn = new QPushButton(this);
        pSetBtn->setFocusPolicy(Qt::NoFocus);
        pSetBtn->setStyleSheet("text-align:top;");
        ui->tableWgtSet->setCellWidget(i, 4, pSetBtn);
        connect(pSetBtn, SIGNAL(clicked()), this, SLOT(setBtnClicked()));
    }
    //列表项与设置值顺序需保持一致
    ui->tableWgtSet->item(0,1)->setText(("电池容量(Ah)"));
    ui->tableWgtSet->item(1,1)->setText(("额定电压(V)"));
    ui->tableWgtSet->item(2,1)->setText(("恒充电压(V)"));
    ui->tableWgtSet->item(3,1)->setText(("恒充电流(A)"));
    ui->tableWgtSet->item(4,1)->setText(("浮充电压(V)"));
    ui->tableWgtSet->item(5,1)->setText(("浮充电流(A)"));
    ui->tableWgtSet->item(6,1)->setText(("过压断开(V)"));
    ui->tableWgtSet->item(7,1)->setText(("充电恢复(V)"));
    ui->tableWgtSet->item(8,1)->setText(("重载设置"));
    ui->tableWgtSet->item(9,1)->setText(("站点号"));
}

void JNSolar::queryBtnClicked()
{
    QPushButton *pBtn = dynamic_cast<QPushButton *>(QObject::sender()); //找到信号发送者
    QModelIndex index = ui->tableWgtSet->indexAt(pBtn->pos());   //定位按钮
    bool bSend = true;

    if(!m_bDcacComm)
    {
        return;
    }
    //PDCAC("query btn press:row%d,colunm%d.\r\n",index.row(), index.column());
    int nRow=index.row();
    switch(nRow)
    {
    case 0:
    ui->label_set->setText("发送查询电池容量请求");
    break;
    case 1:
    ui->label_set->setText("发送查询额定电压请求");
    break;
    case 2:
    ui->label_set->setText("发送查询恒充电压请求");
    break;
    case 3:
    {
        addDcacReadSetMsg(DCAC0_ADDR,0x620C,1);
        ui->label_set->setText("发送查询恒充电流请求");
    }
    break;
    case 4:
    ui->label_set->setText("发送查询浮充电压请求");
    break;
    case 5:
    ui->label_set->setText("发送查询浮充电流请求");
    break;
    case 6:
    ui->label_set->setText("发送查询过压断开请求");
    break;
    case 7:
    ui->label_set->setText("发送查询充电恢复请求");
    break;
    case 8:
    ui->label_set->setText("发送查询重载设置请求");
    break;
    case 9:
    ui->label_set->setText("发送查询站点号请求");
    break;
    default:
        bSend = false;
        break;
    }
    if(bSend)
    {
        QMessageBox::information(this,"JN", "命令已发送",QMessageBox::Ok);
    }
}

void JNSolar::setBtnClicked()
{
    QPushButton *pBtn = dynamic_cast<QPushButton *>(QObject::sender()); //找到信号发送者
    QModelIndex index = ui->tableWgtSet->indexAt(pBtn->pos());   //定位按钮
    QString strValue;
    U16 usValue = 0;
    bool bSend = true;

    if(!m_bDcacComm)
    {
        return;
    }
    //PDCAC("set btn press:row%d,colunm%d.\r\n",index.row(), index.column());
    int nRow=index.row();
    switch(nRow)
    {
    case 0:
    ui->label_set->setText("发送设置电池容量命令");
    break;
    case 1:
    ui->label_set->setText("发送设置额定电压命令");
    break;
    case 2:
    ui->label_set->setText("发送设置恒充电压命令");
    break;
    case 3:
    {
        strValue = ui->tableWgtSet->item(nRow, 2)->text();
        usValue = strValue.toUShort() * 10;
        addDcacSetMsg(DCAC0_ADDR, 0x620C, usValue); //待设置参数值上下限
        ui->label_set->setText("发送设置恒充电流命令");
    }
    break;
    case 4:
    ui->label_set->setText("发送设置浮充电压命令");
    break;
    case 5:
    ui->label_set->setText("发送设置浮充电流命令");
    break;
    case 6:
    ui->label_set->setText("发送设置过压断开命令");
    break;
    case 7:
    ui->label_set->setText("发送设置充电恢复命令");
    break;
    case 8:
    ui->label_set->setText("发送设置重载设置命令");
    break;
    case 9:
    ui->label_set->setText("发送设置站点号命令");
    break;
    default:
        bSend = false;
        break;
    }
    if(bSend)
    {
        QMessageBox::information(this,"JN", "命令已发送",QMessageBox::Ok);
    }

}

void JNSolar::updateSetData()
{
    //if(m_bDcacComm)
    {
        if(m_stDcacWrReg.ucBatMaxChaCurrFlag)
        {
            m_stDcacWrReg.ucBatMaxChaCurrFlag = false;
            ui->tableWgtSet->item(3,2)->setText(QString::number(m_stDcacWrReg.usBatMaxChaCurr/10));
        }
        if(m_stDcacWrReg.ucOnOffModeFlag)
        {
            m_stDcacWrReg.ucOnOffModeFlag = false;
        }
        if(m_stDcacWrReg.ucChaOnOffCmdFlag)
        {
            m_stDcacWrReg.ucChaOnOffCmdFlag = false;
        }
    }
}

void JNSolar::on_tabwgtDcacSta_cellDoubleClicked(int row, int column)
{
    QString strAlarm;
    int i=0;

    if(!m_bDcacComm)
    {
        return;
    }
    strAlarm.clear();
    if(row == 6)    //fault code 1
    {
        for(i=0; i<16; i++)
        {
            if(m_stDcacReg.unFaultCode1.usFaultCode1 & (1<<i))
            {
                strAlarm += astrFCode1[i];
            }
        }
    }
    else if(row == 12)      //fault code 3
    {
        for(i=0; i<16; i++)
        {
            if(m_stDcacReg.unFaultCode3.usFaultCode3 & (1<<i))
            {
                strAlarm += astrFCode3[i];
            }
        }
    }
    else if(row == 19)      //fault code 2
    {
        for(i=0; i<16; i++)
        {
            if(m_stDcacReg.unFaultCode2.usFaultCode2 & (1<<i))
            {
                strAlarm += astrFCode2[i];
            }
        }
    }
    if(!strAlarm.isEmpty())
    {
        QMessageBox::information(this,"Fault Info", strAlarm,QMessageBox::Ok);
    }
}

void JNSolar::on_btnChargeOn_clicked()
{
    QMessageBox::StandardButton rb = QMessageBox::question(NULL, "JN", "确定发送开启命令?", QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if(rb == QMessageBox::No)
    {
        return;
    }
    addDcacSetMsg(DCAC0_ADDR, 0x620E, 1);
    ui->label_set->setText("发送设置充电开启命令");
}

void JNSolar::on_btnChargeOff_clicked()
{
    QMessageBox::StandardButton rb = QMessageBox::question(NULL, "JN", "确定发送关闭命令?", QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if(rb == QMessageBox::No)
    {
        return;
    }
    addDcacSetMsg(DCAC0_ADDR, 0x620E, 0);
    ui->label_set->setText("发送设置充电关闭命令");
}

void JNSolar::on_btnStaUp_clicked()
{
    int nPos=ui->tabwgtDcacSta->verticalScrollBar()->sliderPosition();

    if(nPos > 5)   //13 rows per page
    {
        ui->tabwgtDcacSta->verticalScrollBar()->setSliderPosition(nPos - 5);
    }
    else
    {
        ui->tabwgtDcacSta->verticalScrollBar()->setSliderPosition(0);
    }
}

void JNSolar::on_btnStaDown_clicked()
{
    int nPos=ui->tabwgtDcacSta->verticalScrollBar()->sliderPosition();
    int nRows = ui->tabwgtDcacSta->rowCount();

    if((nPos+5) < nRows)   //13 rows per page
    {
        ui->tabwgtDcacSta->verticalScrollBar()->setSliderPosition(nPos + 5);
    }
    else
    {
        ui->tabwgtDcacSta->verticalScrollBar()->setSliderPosition(nRows);
    }
}
