/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mathom Notes: import selectable text from PDFs into editable HtmlContent.
 * This does not recognize text in scanned images (OCR/HTR is future work).
 */

#include "softwareimporters.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QList>
#include <QProgressDialog>
#include <QRectF>
#include <QStringList>
#include <QVBoxLayout>

#include <KLocalizedString>
#include <KMessageBox>

#include <poppler-qt6.h>

#include "basketfactory.h"
#include "basketscene.h"
#include "bnpview.h"
#include "global.h"
#include "note.h"
#include "notefactory.h"
#include "tools.h"

namespace
{
class PdfImportDialog final : public QDialog
{
public:
    enum class SplitMode { PerPage, EntireDocument };
    enum class TextOrder { Reading, Physical };

    explicit PdfImportDialog(bool hasCurrentBasket, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(i18n("Import PDF Document"));
        setObjectName(QStringLiteral("ImportPdfFile"));
        setModal(true);

        auto *layout = new QVBoxLayout(this);
        auto *info = new QLabel(
            i18n("This import creates editable Mathoms from selectable PDF text. "
                 "Scanned pages require OCR; images and PDF formatting are not imported."),
            this);
        info->setWordWrap(true);
        layout->addWidget(info);

        auto *splitGroup = new QGroupBox(i18n("PDF page grouping"), this);
        auto *splitLayout = new QVBoxLayout(splitGroup);
        m_split = new QComboBox(splitGroup);
        m_split->addItem(i18n("One Mathom per PDF page"),
                         static_cast<int>(SplitMode::PerPage));
        m_split->addItem(i18n("Entire document in one Mathom"),
                         static_cast<int>(SplitMode::EntireDocument));
        splitLayout->addWidget(m_split);
        layout->addWidget(splitGroup);

        auto *orderGroup = new QGroupBox(i18n("PDF text extraction order"), this);
        auto *orderLayout = new QVBoxLayout(orderGroup);
        m_order = new QComboBox(orderGroup);
        m_order->addItem(i18n("Reading order (recommended)"),
                         static_cast<int>(TextOrder::Reading));
        m_order->addItem(i18n("Preserve physical page layout"),
                         static_cast<int>(TextOrder::Physical));
        orderLayout->addWidget(m_order);
        layout->addWidget(orderGroup);

        auto *destinationGroup = new QGroupBox(i18n("Import destination"), this);
        auto *destinationLayout = new QVBoxLayout(destinationGroup);
        m_destination = new QComboBox(destinationGroup);
        using Dest = TextFileImportDialog::Destination;
        if (hasCurrentBasket) {
            m_destination->addItem(i18n("Current Page"),
                                   static_cast<int>(Dest::CurrentPage));
            m_destination->addItem(i18n("New Page in the current location"),
                                   static_cast<int>(Dest::NewPage));
        }
        m_destination->addItem(i18n("New Mathom-House"),
                               static_cast<int>(Dest::NewHouse));
        m_destination->setCurrentIndex(m_destination->count() - 1);
        destinationLayout->addWidget(m_destination);
        layout->addWidget(destinationGroup);

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    TextFileImportDialog::Destination destination() const
    {
        return static_cast<TextFileImportDialog::Destination>(
            m_destination->currentData().toInt());
    }

    SplitMode splitMode() const
    {
        return static_cast<SplitMode>(m_split->currentData().toInt());
    }

    TextOrder textOrder() const
    {
        return static_cast<TextOrder>(m_order->currentData().toInt());
    }

private:
    QComboBox *m_destination;
    QComboBox *m_split;
    QComboBox *m_order;
};
} // namespace

void SoftwareImporters::importPdfFile()
{
    const QString fileName = QFileDialog::getOpenFileName(
        Global::bnpView,
        i18n("Import PDF Document"),
        QString(),
        i18n("PDF files (*.pdf);;All files (*)"));
    if (fileName.isEmpty())
        return;

    BasketScene *currentBasket = Global::bnpView->currentBasket();
    if (currentBasket && !currentBasket->isLoaded())
        currentBasket->load();

    const bool mayUseCurrentBasket = currentBasket && !currentBasket->isLocked();
    PdfImportDialog dialog(mayUseCurrentBasket, Global::bnpView);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Extract everything before creating a destination, so unreadable or
    // scanned-only PDFs cannot leave behind empty Mathom-Houses or Pages.
    const auto document = Poppler::Document::load(fileName);
    if (!document) {
        KMessageBox::error(Global::bnpView, i18n("Cannot open this PDF file."));
        return;
    }
    if (document->isLocked()) {
        KMessageBox::error(Global::bnpView,
            i18n("This PDF is password protected and cannot be imported yet."));
        return;
    }
    if (!document->okToCopy()) {
        KMessageBox::error(Global::bnpView,
            i18n("This PDF does not permit text extraction."));
        return;
    }

    const int pageCount = document->numPages();
    if (pageCount <= 0) {
        KMessageBox::error(Global::bnpView, i18n("This PDF contains no pages."));
        return;
    }

    QStringList extractedPages;
    QList<int> originalPageNumbers;
    int scannedPages = 0;
    qint64 characters = 0;
    constexpr qint64 MaxImportedCharacters = 16000000;

    QProgressDialog progress(i18n("Reading PDF text..."),
                             i18n("Cancel"),
                             0, pageCount, Global::bnpView);
    progress.setWindowTitle(i18n("Import PDF Document"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    const Poppler::Page::TextLayout textLayout =
        dialog.textOrder() == PdfImportDialog::TextOrder::Physical
            ? Poppler::Page::PhysicalLayout
            : Poppler::Page::ReadingOrder;

    for (int i = 0; i < pageCount; ++i) {
        QCoreApplication::processEvents();
        if (progress.wasCanceled())
            return;

        const auto page = document->page(i);
        if (!page) {
            KMessageBox::error(Global::bnpView,
                i18n("Cannot read PDF page %1.", i + 1));
            return;
        }

        QString text = page->text(QRectF(), textLayout);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

        if (!text.trimmed().isEmpty()) {
            characters += text.size();
            if (characters > MaxImportedCharacters) {
                KMessageBox::error(Global::bnpView,
                    i18n("This PDF contains too much text for a single import."));
                return;
            }
            extractedPages.append(text);
            originalPageNumbers.append(i + 1);
        } else {
            ++scannedPages;
        }

        progress.setValue(i + 1);
        if (progress.wasCanceled())
            return;
    }
    progress.close();

    if (extractedPages.isEmpty()) {
        KMessageBox::information(Global::bnpView,
            i18n("This PDF contains no selectable text. Scanned PDFs require OCR."));
        return;
    }

    QStringList parts;
    if (dialog.splitMode() == PdfImportDialog::SplitMode::PerPage) {
        parts = extractedPages;
    } else {
        QString combined;
        for (qsizetype i = 0; i < extractedPages.size(); ++i) {
            if (!combined.isEmpty())
                combined.append(QStringLiteral("\n\n"));
            if (pageCount > 1)
                combined.append(i18n("Page %1", originalPageNumbers.at(i))
                                + QLatin1Char('\n'));
            combined.append(extractedPages.at(i));
        }
        parts.append(combined);
    }

    using Dest = TextFileImportDialog::Destination;
    const Dest destination = dialog.destination();
    BasketScene *basket = currentBasket;
    if (destination == Dest::NewHouse) {
        const QString title = i18nc("From document.pdf", "From %1",
                                    QFileInfo(fileName).fileName());
        basket = BasketFactory::newBasket(QStringLiteral("application-pdf"), title);
        if (!basket)
            return;
        basket->load();
    } else if (!basket || basket->isLocked()) {
        KMessageBox::error(Global::bnpView,
            i18n("The target location is not available or is locked."));
        return;
    }

    if (destination == Dest::NewPage) {
        const QString newPageId = basket->createPage();
        if (newPageId.isEmpty()) {
            KMessageBox::error(Global::bnpView,
                i18n("Cannot create a new Page for the import."));
            return;
        }
        basket->renamePage(newPageId, QFileInfo(fileName).completeBaseName());
    }

    // The same disposition rules as TXT DEV3: new destinations start with
    // one column; pre-existing Pages retain their current disposition.
    if (destination != Dest::CurrentPage)
        basket->setCurrentPageDisposition(false, 1);

    Note *first = nullptr;
    Note *last = nullptr;
    int imported = 0;
    for (const QString &part : parts) {
        // Reuse the normal Mathom HtmlContent renderer, never special plain
        // text notes or HTML supplied from the untrusted PDF itself.
        const QString html = QStringLiteral(
            "<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\">"
            "<meta name=\"qrichtext\" content=\"1\" /></head><body>%1</body></html>")
                                 .arg(Tools::textToHTMLWithoutP(part));
        Note *note = NoteFactory::createNoteHtml(html, basket);
        if (!note)
            break;
        if (last) {
            last->setNext(note);
            note->setPrev(last);
        } else {
            first = note;
        }
        last = note;
        ++imported;
    }

    if (first) {
        basket->insertCreatedNote(first);
        basket->unselectAll();
        basket->setFocusedNote(first);
        basket->relayoutNotes();
        basket->save();
    }

    if (imported != parts.size()) {
        KMessageBox::error(Global::bnpView,
            i18n("The PDF import was interrupted: %1 of %2 Mathoms were created.",
                 imported, parts.size()));
        return;
    }

    QString result = i18n("Imported %1 Mathoms from %2.",
                          imported, QFileInfo(fileName).fileName());
    if (scannedPages > 0) {
        result += QStringLiteral("\n")
               + i18n("Pages without selectable text skipped: %1. OCR is not available yet.",
                      scannedPages);
    }
    KMessageBox::information(Global::bnpView, result);
}
