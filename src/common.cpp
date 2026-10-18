/**
 * SPDX-FileCopyrightText: (C) 2003 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "common.h"

#include <QByteArray>
#include <QFile>
#include <QSaveFile>
#include <QString>

#include <KLocalizedString>

#include "bnpview.h"
#include "global.h"

bool FileStorage::loadFromFile(const QString &fullPath, QString *string)
{
    QByteArray array;

    if (loadFromFile(fullPath, &array)) {
        *string = QString::fromUtf8(array.data(), array.size());
        return true;
    } else
        return false;
}

bool FileStorage::loadFromFile(const QString &fullPath, QByteArray *array)
{
    QFile file(fullPath);
    bool encrypted = false;

    if (file.open(QIODevice::ReadOnly)) {
        *array = file.readAll();
        const QByteArray magic = "-----BEGIN PGP MESSAGE-----";
        int i = 0;

        if (array->size() > magic.size())
            for (i = 0; array->at(i) == magic[i]; ++i)
                ;
        if (i == magic.size()) {
            encrypted = true;
        }
        file.close();
#ifdef HAVE_LIBGPGME
        if (encrypted) {
            QByteArray tmp(*array);

            tmp.detach();
            // Only use gpg-agent for private key encryption since it doesn't
            // cache password used in symmetric encryption.
            m_gpg->setUseGnuPGAgent(Settings::useGnuPGAgent() && m_encryptionType == PrivateKeyEncryption);
            if (m_encryptionType == PrivateKeyEncryption)
                m_gpg->setText(i18n("Please enter the password for the following private key:"), false);
            else
                m_gpg->setText(i18n("Please enter the password for <b>%1</b>:", basketName()), false); // Used when decrypting
            return m_gpg->decrypt(tmp, array);
        }
#else
        if (encrypted) {
            return false;
        }
#endif
        return true;
    } else
        return false;
}

bool FileStorage::saveToFile(const QString &fullPath, const QString &string, bool isEncrypted)
{
    const QByteArray array = string.toUtf8();
    return saveToFile(fullPath, array, isEncrypted);
}

bool FileStorage::saveToFile(const QString &fullPath, const QByteArray &array, bool isEncrypted)
{
    bool success = true;
    QByteArray tmp;

#ifdef HAVE_LIBGPGME
    if (isEncrypted) {
        QString key;

        // We only use gpg-agent for private key encryption and saving without
        // public key doesn't need one.
        m_gpg->setUseGnuPGAgent(false);
        if (m_encryptionType == PrivateKeyEncryption) {
            key = m_encryptionKey;
            // public key doesn't need password
            m_gpg->setText(QString(), false);
        } else
            m_gpg->setText(i18n("Please assign a password to <b>%1</b>:", basketName()), true); // Used when defining a new password

        success = m_gpg->encrypt(array, length, &tmp, key);
        length = tmp.size();
    } else
        tmp = array;

#else
    success = !isEncrypted;
    if (success)
        tmp = array;
#endif
    /*if (success && (success = file.open(QIODevice::WriteOnly))){
        success = (file.write(tmp) == (Q_LONG)tmp.size());
        file.close();
    }*/

    if (success)
        return safelySaveToFile(fullPath, tmp);
    else
        return false;
}

/**
 * A safer version of saveToFile, that doesn't perform encryption.  To save a
 * file owned by a basket (i.e. a basket or a note file), use saveToFile(), but
 * to save to another file, (e.g. the basket hierarchy), use this function
 * instead.
 */
bool FileStorage::safelySaveToFile(const QString &fullPath, const QByteArray &array)
{
    // One atomic attempt. Persistent model owners arrange timed retries with
    // PendingSave; temporary exports/imports must propagate the failure.
    QSaveFile saveFile(fullPath);
    saveFile.setDirectWriteFallback(false);
    if (saveFile.open(QIODevice::WriteOnly)) {
        if (saveFile.write(array) == array.size()) {
            if (saveFile.commit())
                return true;
        } else {
            saveFile.cancelWriting();
        }
    }

    if (Global::bnpView)
        Q_EMIT Global::bnpView->showErrorMessage(i18n("Error while saving: ") + saveFile.errorString());
    return false;
}

bool FileStorage::safelySaveToFile(const QString &fullPath, const QString &string)
{
    QByteArray bytes = string.toUtf8();
    return safelySaveToFile(fullPath, bytes);
}
