/**
  * @file hidcontroller.cpp
  * @author Sean M. Pappalardo	spappalardo@mixxx.org
  * @date Sun May 1 2011
  * @brief HID controller backend
  *
  */

#include "controllers/hid/hidcontroller.h"

HidReader::HidReader(hid_device* device)
        : QThread(),
          m_pHidDevice(device) {
}

HidReader::~HidReader() {
}

void HidReader::run() {
    m_stop = 0;
    unsigned char *data = new unsigned char[255];
    while (m_stop == 0) {
        // Blocked polling: The only problem with this is that we can't close
        // the device until the block is released, which means the controller
        // has to send more data
        //result = hid_read_timeout(m_pHidDevice, data, 255, -1);

        // This relieves that at the cost of higher CPU usage since we only
        // block for a short while (500ms)
        int result = hid_read_timeout(m_pHidDevice, data, 255, 500);
        if (result > 0) {
            //qDebug() << "Read" << result << "bytes, pointer:" << data;
            QByteArray outData(reinterpret_cast<char*>(data), result);
            emit(incomingData(outData));
        }
    }
    delete [] data;
}

HidController::HidController(const hid_device_info deviceInfo) {

    // Copy required variables from deviceInfo, which will be freed after
    // this class is initialized by caller.
    hid_vendor_id = deviceInfo.vendor_id;
    hid_product_id = deviceInfo.product_id;
    if (deviceInfo.interface_number==-1) {
        // OS/X and windows don't use interface numbers, but usage_page/usage
        hid_usage_page = deviceInfo.usage_page;
        hid_usage = deviceInfo.usage;
        hid_interface_number = -1;
    } else {
        // Linux hidapi does not set value for usage_page or usage and uses
        // interface number to identify subdevices
        hid_usage_page = 0;
        hid_usage = 0;
        hid_interface_number = deviceInfo.interface_number;
    }

#ifdef __LINUX__
    hid_path = strndup(deviceInfo.path, strlen(deviceInfo.path));
#else
    int hid_path_len = strlen(deviceInfo.path);
    hid_path = (char *)malloc(hid_path_len+1);
    strncpy(hid_path, deviceInfo.path, hid_path_len);
    hid_path[hid_path_len] = '\0';
#endif

    // TODO: Verify that this is the correct thing to do and allows a device
    //  with a null serial number to be used
    if (deviceInfo.serial_number!=NULL) {
        hid_serial = new wchar_t[wcslen(deviceInfo.serial_number)+1];
        wcscpy(hid_serial,deviceInfo.serial_number);
    } else {
        hid_serial = new wchar_t[1];
        hid_serial[0] = '\0';
    }

    // TODO: we have a crash here when strings are invalid or NULL
    hid_manufacturer = QString::fromWCharArray(
        deviceInfo.manufacturer_string,
        wcslen(deviceInfo.manufacturer_string)
    );
    hid_product = QString::fromWCharArray(
        deviceInfo.product_string,
        wcslen(deviceInfo.product_string)
    );

    guessDeviceCategory();

    // Set the Unique Identifier to the serial_number
    m_sUID = QString::fromWCharArray(hid_serial,wcslen(hid_serial));

    //Note: We include the last 4 digits of the serial number and the
    // interface number to allow the user (and Mixxx!) to keep track of
    // which is which
    if (hid_interface_number < 0) {
        setDeviceName(
            QString("%1 %2").arg(hid_product)
            .arg(QString::fromWCharArray(hid_serial,wcslen(hid_serial)).right(4)));
    } else {
        setDeviceName(
            QString("%1 %2_%3").arg(hid_product)
            .arg(QString::fromWCharArray(hid_serial,wcslen(hid_serial)).right(4))
            .arg(QString::number(hid_interface_number)));
        m_sUID.append(QString::number(hid_interface_number));
    }

    // All HID devices are full-duplex
    setInputDevice(true);
    setOutputDevice(true);
    m_pReader = NULL;
}

HidController::~HidController() {
    close();
    free(hid_path);
    delete [] hid_serial;
}

void HidController::visit(const MidiControllerPreset* preset) {
    Q_UNUSED(preset);
    // TODO(XXX): throw a hissy fit.
    qDebug() << "ERROR: Attempting to load a MidiControllerPreset to an HidController!";
}

void HidController::visit(const HidControllerPreset* preset) {
    m_preset = *preset;
    // Emit presetLoaded with a clone of the preset.
    emit(presetLoaded(getPreset()));
}

bool HidController::savePreset(const QString fileName) const {
    HidControllerPresetFileHandler handler;
    return handler.save(m_preset, getName(), fileName);
}

bool HidController::matchProductInfo(QHash <QString,QString > info) {
    int value;
    bool ok;
    // Product and vendor match is always required
    value = info["vendor_id"].toInt(&ok,16);
    if (!ok || hid_vendor_id!=value) return false;
    value = info["product_id"].toInt(&ok,16);
    if (!ok || hid_product_id!=value) return false;

    // Optionally check against interface_number / usage_page && usage
    if (hid_interface_number!=-1) {
        value = info["interface_number"].toInt(&ok,16);
        if (!ok || hid_interface_number!=value) return false;
    } else {
        value = info["usage_page"].toInt(&ok,16);
        if (!ok || hid_usage_page!=value) return false;

        value = info["usage"].toInt(&ok,16);
        if (!ok || hid_usage!=value) return false;
    }
    // Match found
    return true;
}
void  HidController::guessDeviceCategory() {
    // This should be done somehow else, I know. But at least we get started with
    // the idea of mapping this information
    QString info;
    if (hid_interface_number==-1) {
        if (hid_usage_page==0x1) {
            switch (hid_usage) {
                case 0x2: info = "Generic HID Mouse"; break;
                case 0x4: info = "Generic HID Joystick"; break;
                case 0x5: info = "Generic HID Gamepad"; break;
                case 0x6: info = "Generic HID Keyboard"; break;
                case 0x8: info = "Gereric HID Multiaxis Controller"; break;
                default:
                    info.sprintf(
                        "Unknown HID Desktop Device 0x%0x/0x%0x",hid_usage_page,hid_usage
                    );
                    break;
            }
        } else if (hid_vendor_id==0x5ac) {
            // Apple laptop special HID devices
            if (hid_product_id==0x8242) {
                info = "HID Infrared Control";
            } else {
                info.sprintf(
                    "Unknown Apple HID Device 0x%0x/0x%0x",hid_usage_page,hid_usage
                );
            }
        } else {
            // Fill in the usage page and usage fields for debugging info
            info.sprintf("HID Unknown Device 0x%0x/0x%0x",hid_usage_page,hid_usage);
        }
    } else {
        // Guess linux device types somehow as well. Or maybe just
        // fill in the interface number?
        info.sprintf("HID Interface Number 0x%0x",hid_interface_number);
    }
    setDeviceCategory(info);
}

int HidController::open() {
    if (isOpen()) {
        qDebug() << "HID device" << getName() << "already open";
        return -1;
    }

    // Open device by path
    if (debugging()) {
        qDebug() << "Opening HID device"
                 << getName() << "by HID path" << hid_path;
    }
    m_pHidDevice = hid_open_path(hid_path);

    // If that fails, try to open device with vendor/product/serial #
    if (m_pHidDevice == NULL) {
        if (debugging())
            qDebug() << "Failed. Trying to open with make, model & serial no:"
                << hid_vendor_id << hid_product_id << hid_serial;
        m_pHidDevice = hid_open(hid_vendor_id,hid_product_id,hid_serial);
    }

    // If it does fail, try without serial number WARNING: This will only open
    // one of multiple identical devices
    if (m_pHidDevice == NULL) {
        qWarning() << "Unable to open specific HID device" << getName()
                   << "Trying now with just make and model."
                   << "(This may only open the first of multiple identical devices.)";
        m_pHidDevice = hid_open(hid_vendor_id, hid_product_id, NULL);
    }

    // If that fails, we give up!
    if (m_pHidDevice == NULL) {
        qWarning()  << "Unable to open HID device" << getName();
        return -1;
    }

    setOpen(true);
    startEngine();

    if (m_pReader != NULL) {
        qWarning() << "HidReader already present for" << getName();
    } else {
        m_pReader = new HidReader(m_pHidDevice);
        m_pReader->setObjectName(QString("HidReader %1").arg(getName()));

        connect(m_pReader, SIGNAL(incomingData(QByteArray)),
                this, SLOT(receive(QByteArray)));

        // Controller input needs to be prioritized since it can affect the
        // audio directly, like when scratching
        m_pReader->start(QThread::HighPriority);
    }

    return 0;
}

int HidController::close() {
    if (!isOpen()) {
        qDebug() << "HID device" << getName() << "already closed";
        return -1;
    }

    qDebug() << "Shutting down HID device" << getName();

    // Stop the reading thread
    if (m_pReader == NULL) {
        qWarning() << "HidReader not present for" << getName()
                   << "yet the device is open!";
    } else {
        disconnect(m_pReader, SIGNAL(incomingData(QByteArray)),
                   this, SLOT(receive(QByteArray)));
        m_pReader->stop();
        hid_set_nonblocking(m_pHidDevice, 1);   // Quit blocking
        if (debugging()) qDebug() << "  Waiting on reader to finish";
        m_pReader->wait();
        delete m_pReader;
        m_pReader = NULL;
    }

    // Stop controller engine here to ensure it's done before the device is closed
    //  incase it has any final parting messages
    stopEngine();

    // Close device
    if (debugging()) {
        qDebug() << "  Closing device";
    }
    hid_close(m_pHidDevice);
    setOpen(false);
    return 0;
}

void HidController::send(QList<int> data, unsigned int length, unsigned int reportID) {
    Q_UNUSED(length);
    QByteArray temp;
    foreach (int datum, data) {
        temp.append(datum);
    }
    send(temp, reportID);
}

void HidController::send(QByteArray data) {
    send(data, 0);
}

void HidController::send(QByteArray data, unsigned int reportID) {
    // Append the Report ID to the beginning of data[] per the API..
    data.prepend(reportID);

    int result = hid_write(m_pHidDevice, (unsigned char*)data.constData(), data.size());
    QString serial = QString::fromWCharArray(hid_serial);
    if (result == -1) {
        if (debugging()) {
            qWarning() << "Unable to send data to" << getName()
                       << "serial #" << serial << ":"
                       << QString::fromWCharArray(hid_error(m_pHidDevice));
        } else {
            qWarning() << "Unable to send data to" << getName() << ":"
                       << QString::fromWCharArray(hid_error(m_pHidDevice));
        }
    } else if (debugging()) {
        qDebug() << result << "bytes sent to" << getName()
                 << "serial #" << serial
                 << "(including report ID of" << reportID << ")";
    }
}
