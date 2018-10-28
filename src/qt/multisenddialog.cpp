#include "addressbookpage.h"
#include "base58.h"
#include "init.h"
#include "walletmodel.h"
#include <QLineEdit>
#include <QMessageBox>
#include <QStyle>
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;

MultiSendDialog::MultiSendDialog(QWidget* parent) : QDialog(parent),
                                                    ui(new Ui::MultiSendDialog),
                                                    model(0)
{
    ui->setupUi(this);

    updateCheckBoxes();
}

MultiSendDialog::~MultiSendDialog()
{
    delete ui;
}

void MultiSendDialog::setModel(WalletModel* model)
{
    this->model = model;
}

void MultiSendDialog::setAddress(const QString& address)
{
    setAddress(address, ui->multiSendAddressEdit);
}

void MultiSendDialog::setAddress(const QString& address, QLineEdit* addrEdit)
{
    addrEdit->setText(address);
    addrEdit->setFocus();
}

void MultiSendDialog::updateCheckBoxes()
{
    ui->multiSendStakeCheckBox->setChecked(pwalletMain->fMultiSendStake);
    ui->multiSendMasternodeCheckBox->setChecked(pwalletMain->fMultiSendMasternodeReward);
    ui->multiSendSupernodeCheckBox->setChecked(pwalletMain->fMultiSendSupernodeReward);
}
