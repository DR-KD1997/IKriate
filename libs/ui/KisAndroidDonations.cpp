/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisAndroidDonations.h"

#include "KisApplication.h"

// IKriate: Krita's Android build sells the Krita Foundation's supporter
// products through Google Play. Those products belong to the Foundation and
// are not offered by IKriate, so the billing code has been removed and this
// class only reports that in-app purchases are unavailable. The interface is
// kept so the rest of the application compiles unchanged.

KisAndroidDonations *KisAndroidDonations::instance()
{
    KisApplication *app = qobject_cast<KisApplication *>(KisApplication::instance());
    return app ? app->androidDonations() : nullptr;
}

KisAndroidDonations::KisAndroidDonations(QObject *parent)
    : QObject(parent)
    , m_state(State::Unavailable)
{
}

void KisAndroidDonations::setLoaded(bool)
{
}

void KisAndroidDonations::setLoadingText(const QString &)
{
}

void KisAndroidDonations::showDonationDialog(bool)
{
}

bool KisAndroidDonations::shouldShowSupporterBadge() const
{
    return false;
}

void KisAndroidDonations::setShowDonationManagementDialogPending(bool)
{
    m_showDonationManagementDialogPending = false;
}

QVector<KisSupporterProduct> KisAndroidDonations::getCurrentProducts() const
{
    return {};
}

void KisAndroidDonations::startBillingFlowFor(const QString &, const QString &)
{
}

void KisAndroidDonations::slotStartDonationFlow()
{
}

void KisAndroidDonations::slotManageSubscriptions()
{
}

void KisAndroidDonations::slotManageSubscription(const QString &)
{
}

void KisAndroidDonations::slotUpdateState(int, long long)
{
}

bool KisAndroidDonations::isProductOwned(const QString &) const
{
    return false;
}

void KisAndroidDonations::syncState()
{
}
