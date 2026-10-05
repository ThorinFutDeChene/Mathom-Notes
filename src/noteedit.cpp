/**
 * SPDX-FileCopyrightText: (C) 2003 Sébastien Laoût <slaout@linux62.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "noteedit.h"
#include "accessibilitysettings.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGraphicsProxyWidget>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextCharFormat>
#include <QTableWidget>
#include <QTimer>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QVariant>

#include <KActionCollection>
#include <KColorCombo>
#include <KConfig>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KIconButton>
#include <KLineEdit>
#include <KLocalizedString>
#include <KMainWindow>
#include <KMessageBox>
#include <KService>
#include <KToggleAction>
#include <KToolBar>
#include <KUrlRequester>

#include "basketlistview.h"
#include "basketscene.h"
#include "bnpview.h"
#include "focusedwidgets.h"
#include "global.h"
#include "mathomicons.h"
#include "note.h"
#include "notecontent.h"
#include "notefactory.h"
#include "settings.h"
#include "spreadsheetcontent.h"
#include "tools.h"
#include "variouswidgets.h"

#include <basket_debug.h>

/** class NoteEditor: */

NoteEditor::NoteEditor(NoteContent *noteContent)
{
    m_isEmpty = false;
    m_canceled = false;
    m_widget = nullptr;
    m_textEdit = nullptr;
    m_lineEdit = nullptr;
    m_noteContent = noteContent;
}

NoteEditor::~NoteEditor()
{
    if (!m_widget)
        return;

    if (m_deferWidgetDeletion) {
        // Composite editors can still be inside a Qt Widgets mouse/focus event
        // when BasketScene closes the Mathom. Destroying the proxy synchronously
        // can leave Qt dispatching the current event to an already freed widget.
        // Remove/hide it immediately, but let Qt destroy the widget tree after
        // the current event has returned to the event loop.
        m_widget->hide();
        m_widget->deleteLater();
        m_widget = nullptr;
        return;
    }

    delete m_widget;
}

Note *NoteEditor::note()
{
    return m_noteContent->note();
}

void NoteEditor::setCursorTo(const QPointF &pos)
{
    // clicked comes from the QMouseEvent, which is in item's coordinate system.
    if (m_textEdit) {
        QPointF currentPos = note()->mapFromScene(pos);
        QPointF deltaPos = m_textEdit->pos() - note()->pos();
        m_textEdit->setTextCursor(m_textEdit->cursorForPosition((currentPos - deltaPos).toPoint()));
    }
}

void NoteEditor::startSelection(const QPointF &pos)
{
    if (m_textEdit) {
        if (auto *focusedTextEdit =
                dynamic_cast<FocusedTextEdit *>(m_textEdit)) {
            focusedTextEdit->clearMultiSelection();
        }

        QPointF currentPos = note()->mapFromScene(pos);
        QPointF deltaPos = m_textEdit->pos() - note()->pos();
        m_textEdit->setTextCursor(m_textEdit->cursorForPosition((currentPos - deltaPos).toPoint()));
    }
}

void NoteEditor::updateSelection(const QPointF &pos)
{
    if (m_textEdit) {
        QPointF currentPos = note()->mapFromScene(pos);
        QPointF deltaPos = m_textEdit->pos() - note()->pos();

        QTextCursor cursor = m_textEdit->cursorForPosition((currentPos - deltaPos).toPoint());
        QTextCursor currentCursor = m_textEdit->textCursor();
        // select the text
        currentCursor.setPosition(cursor.position(), QTextCursor::KeepAnchor);
        // update the cursor
        m_textEdit->setTextCursor(currentCursor);
    }
}

void NoteEditor::endSelection(const QPointF & /*pos*/)
{
    // For TextEdit inside GraphicsScene selectionChanged() is only generated for the first selected char -
    // thus we need to call it manually after selection is finished
    if (auto *textEdit = dynamic_cast<FocusedTextEdit *>(m_textEdit))
        textEdit->onSelectionChanged();
}

void NoteEditor::paste(const QPointF &pos, QClipboard::Mode mode)
{
    if (auto *textEdit = dynamic_cast<FocusedTextEdit *>(m_textEdit)) {
        setCursorTo(pos);
        textEdit->paste(mode);
    }
}

void NoteEditor::connectActions(BasketScene *scene)
{
    if (m_textEdit) {
        connect(m_textEdit, &QTextEdit::textChanged, scene, &BasketScene::selectionChangedInEditor);
        connect(m_textEdit, &QTextEdit::textChanged, scene, &BasketScene::contentChangedInEditor);
        connect(m_textEdit, &KTextEdit::textChanged, scene, &BasketScene::placeEditorAndEnsureVisible);
        connect(m_textEdit, &QTextEdit::selectionChanged, scene, &BasketScene::selectionChangedInEditor);

    } else if (m_lineEdit) {
        connect(m_lineEdit, &QLineEdit::textChanged, scene, &BasketScene::selectionChangedInEditor);
        connect(m_lineEdit, &QLineEdit::textChanged, scene, &BasketScene::contentChangedInEditor);
        connect(m_lineEdit, &QLineEdit::selectionChanged, scene, &BasketScene::selectionChangedInEditor);
    }
}

NoteEditor *NoteEditor::editNoteContent(NoteContent *noteContent, QWidget *parent)
{
    auto *textContent = dynamic_cast<TextContent *>(noteContent);
    if (textContent)
        return new TextEditor(textContent, parent);

    auto *htmlContent = dynamic_cast<HtmlContent *>(noteContent);
    if (htmlContent)
        return new HtmlEditor(htmlContent, parent);

    auto *imageContent = dynamic_cast<ImageContent *>(noteContent);
    if (imageContent)
        return new ImageEditor(imageContent, parent);

    auto *animationContent = dynamic_cast<AnimationContent *>(noteContent);
    if (animationContent)
        return new AnimationEditor(animationContent, parent);

    auto *fileContent = dynamic_cast<FileContent *>(noteContent); // Same for SoundContent
    if (fileContent)
        return new FileEditor(fileContent, parent);

    auto *linkContent = dynamic_cast<LinkContent *>(noteContent);
    if (linkContent)
        return new LinkEditor(linkContent, parent);

    auto *crossReferenceContent = dynamic_cast<CrossReferenceContent *>(noteContent);
    if (crossReferenceContent)
        return new CrossReferenceEditor(crossReferenceContent, parent);

    auto *launcherContent = dynamic_cast<LauncherContent *>(noteContent);
    if (launcherContent)
        return new LauncherEditor(launcherContent, parent);

    auto *colorContent = dynamic_cast<ColorContent *>(noteContent);
    if (colorContent)
        return new ColorEditor(colorContent, parent);

    auto *spreadsheetContent = dynamic_cast<SpreadsheetContent *>(noteContent);
    if (spreadsheetContent)
        return new SpreadsheetEditor(spreadsheetContent, parent);

    auto *unknownContent = dynamic_cast<UnknownContent *>(noteContent);
    if (unknownContent)
        return new UnknownEditor(unknownContent, parent);

    return nullptr;
}

void NoteEditor::setInlineEditor(QWidget *inlineEditor)
{
    if (!m_widget) {
        m_widget = new QGraphicsProxyWidget();
    } else if (m_widget->widget() != inlineEditor) {
        QWidget *w = m_widget->widget();
        m_widget->setWidget(nullptr);

        if (w) {
            w->hide();
            w->deleteLater();
        }
    }
    m_widget->setWidget(inlineEditor);
    m_widget->setZValue(500);
    // m_widget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    m_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_textEdit = nullptr;
    m_lineEdit = nullptr;
    auto *textEdit = dynamic_cast<KTextEdit *>(inlineEditor);
    if (textEdit) {
        m_textEdit = textEdit;
    } else {
        auto *lineEdit = dynamic_cast<QLineEdit *>(inlineEditor);
        if (lineEdit) {
            m_lineEdit = lineEdit;
        }
    }
}

/** class TextEditor: */

TextEditor::TextEditor(TextContent *textContent, QWidget * /*parent*/)
    : NoteEditor(textContent)
    , m_textContent(textContent)
{
    auto *textEdit = new FocusedTextEdit(/*disableUpdatesOnKeyPress=*/true, nullptr);
    textEdit->setLineWidth(0);
    textEdit->setMidLineWidth(0);
    textEdit->setFrameStyle(QFrame::Box);
    QPalette palette;
    palette.setColor(textEdit->backgroundRole(), note()->backgroundColor());
    palette.setColor(textEdit->foregroundRole(), note()->textColor());
    textEdit->setPalette(palette);

    textEdit->setFont(note()->font());
    textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    if (Settings::spellCheckTextNotes())
        textEdit->setCheckSpellingEnabled(true);
    textEdit->setPlainText(m_textContent->text());

    // Mathom accessibility layer: visual adaptation only.
    textEdit->setProperty(
        "mathomAccessibilityEditor",
        true);

    textEdit->setProperty(
        "mathomOriginalFont",
        QVariant::fromValue(textEdit->font()));

    AccessibilitySettings::applyToTextEditor(textEdit);

    /*
     * Mark this editor as a plain-text Mathom editor.
     * Accessibility settings affect presentation only.
     */
    textEdit->setProperty(
        "mathomAccessibilityPlainText",
        true);

    textEdit->setProperty(
        "mathomAccessibilityOriginalFont",
        QVariant::fromValue(note()->font()));

    AccessibilitySettings::applyToTextEditor(textEdit);

    // Not sure if the following comment is still true
    // FIXME: Sometimes, the cursor flicker at ends before being positioned where clicked (because qApp->processEvents() I think)
    textEdit->moveCursor(QTextCursor::End);
    textEdit->verticalScrollBar()->setCursor(Qt::ArrowCursor);
    setInlineEditor(textEdit);
    connect(textEdit, &FocusedTextEdit::escapePressed, this, &TextEditor::askValidation);
    connect(textEdit, &FocusedTextEdit::mouseEntered, this, &TextEditor::mouseEnteredEditorWidget);

    connect(textEdit, &FocusedTextEdit::cursorPositionChanged, textContent->note()->basket(), &BasketScene::editorCursorPositionChanged);
    // In case it is a very big note, the top is displayed and Enter is pressed: the cursor is on bottom, we should enure it visible:
    QTimer::singleShot(0, textContent->note()->basket(), &BasketScene::editorCursorPositionChanged);
}

TextEditor::~TextEditor() = default;

void TextEditor::autoSave(bool toFileToo)
{
    bool autoSpellCheck = true;
    if (toFileToo) {
        if (Settings::spellCheckTextNotes() != textEdit()->checkSpellingEnabled()) {
            Settings::setSpellCheckTextNotes(textEdit()->checkSpellingEnabled());
            Settings::saveConfig();
        }

        autoSpellCheck = textEdit()->checkSpellingEnabled();
        textEdit()->setCheckSpellingEnabled(false);
    }

    m_textContent->setText(textEdit()->toPlainText());

    if (toFileToo) {
        m_textContent->saveToFile();
        m_textContent->setEdited();
        textEdit()->setCheckSpellingEnabled(autoSpellCheck);
    }
}

void TextEditor::validate()
{
    if (Settings::spellCheckTextNotes() != textEdit()->checkSpellingEnabled()) {
        Settings::setSpellCheckTextNotes(textEdit()->checkSpellingEnabled());
        Settings::saveConfig();
    }

    textEdit()->setCheckSpellingEnabled(false);
    if (textEdit()->document()->isEmpty())
        setEmpty();
    m_textContent->setText(textEdit()->toPlainText());
    m_textContent->saveToFile();
    m_textContent->setEdited();

    note()->setWidth(0);
}

/** class HtmlEditor: */

HtmlEditor::HtmlEditor(HtmlContent *htmlContent, QWidget * /*parent*/)
    : NoteEditor(htmlContent)
    , m_htmlContent(htmlContent)
{
    auto *textEdit = new FocusedTextEdit(/*disableUpdatesOnKeyPress=*/true, nullptr);
    textEdit->setLineWidth(0);
    textEdit->setMidLineWidth(0);
    textEdit->setFrameStyle(QFrame::Box);
    textEdit->setAutoFormatting(Settings::autoBullet() ? QTextEdit::AutoAll : QTextEdit::AutoNone);

    QPalette palette;
    palette.setColor(textEdit->backgroundRole(), note()->backgroundColor());
    palette.setColor(textEdit->foregroundRole(), note()->textColor());
    textEdit->setPalette(palette);

    textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textEdit->setHtml(Tools::detectCrossReferences(m_htmlContent->html(), /*userLink=*/true));

    /*
     * Accessibility layer for standard rich-text Mathoms.
     * This changes presentation only, never the stored HTML.
     */
    textEdit->setProperty(
        "mathomAccessibilityEditor",
        true);

    textEdit->setProperty(
        "mathomAccessibilityRichText",
        true);

    AccessibilitySettings::applyToTextEditor(textEdit);
    textEdit->moveCursor(QTextCursor::End);
    textEdit->verticalScrollBar()->setCursor(Qt::ArrowCursor);
    setInlineEditor(textEdit);

    connect(textEdit, &FocusedTextEdit::mouseEntered, this, &HtmlEditor::mouseEnteredEditorWidget);
    connect(textEdit, &FocusedTextEdit::escapePressed, this, &HtmlEditor::askValidation);

    connect(InlineEditors::instance()->richTextFont, &QFontComboBox::currentFontChanged, this, &HtmlEditor::onFontSelectionChanged);
    connect(InlineEditors::instance()->richTextFontSize, &FontSizeCombo::sizeChanged, textEdit, &FocusedTextEdit::applyFontPointSize);
    connect(InlineEditors::instance()->richTextColor, &KColorCombo::activated, textEdit, &FocusedTextEdit::applyTextColor);

    connect(InlineEditors::instance()->focusWidgetFilter, &FocusWidgetFilter::escapePressed, textEdit, [&textEdit]() {
        textEdit->setFocus();
    });
    connect(InlineEditors::instance()->focusWidgetFilter, &FocusWidgetFilter::returnPressed, textEdit, [&textEdit]() {
        textEdit->setFocus();
    });
    BasketScene *basket = htmlContent->note()->basket();

    connect(InlineEditors::instance()->richTextFont, &QFontComboBox::activated, textEdit, [basket]() {
        QTimer::singleShot(100, basket, &BasketScene::focusEditor);
    });

    connect(InlineEditors::instance()->richTextFontSize, &QComboBox::activated, textEdit, [basket]() {
        QTimer::singleShot(100, basket, &BasketScene::focusEditor);
    });

    connect(InlineEditors::instance()->richTextFontSize, &FontSizeCombo::returnPressed2, textEdit, [basket]() {
        QTimer::singleShot(100, basket, &BasketScene::focusEditor);
    });

    connect(InlineEditors::instance()->richTextFontSize, &FontSizeCombo::escapePressed, textEdit, [basket]() {
        QTimer::singleShot(100, basket, &BasketScene::focusEditor);
    });

    connect(InlineEditors::instance()->richTextColor, &KColorCombo::activated, textEdit, [basket](const QColor &) {
        QTimer::singleShot(100, basket, &BasketScene::focusEditor);
    });

    connect(textEdit, &QTextEdit::cursorPositionChanged, this, &HtmlEditor::cursorPositionChanged);
    connect(textEdit, &QTextEdit::currentCharFormatChanged, this, &HtmlEditor::charFormatChanged);

    connect(InlineEditors::instance()->richTextBold, &QAction::triggered, this, &HtmlEditor::setBold);
    connect(InlineEditors::instance()->richTextItalic, &QAction::triggered, textEdit, &FocusedTextEdit::applyFontItalic);
    connect(InlineEditors::instance()->richTextUnderline, &QAction::triggered, textEdit, &FocusedTextEdit::applyFontUnderline);
    connect(InlineEditors::instance()->richTextSuper, &QAction::triggered, this, &HtmlEditor::setSuperscript);
    connect(InlineEditors::instance()->richTextSub, &QAction::triggered, this, &HtmlEditor::setSubscript);
    connect(InlineEditors::instance()->richTextLeft, &QAction::triggered, this, &HtmlEditor::setLeft);
    connect(InlineEditors::instance()->richTextCenter, &QAction::triggered, this, &HtmlEditor::setCentered);
    connect(InlineEditors::instance()->richTextRight, &QAction::triggered, this, &HtmlEditor::setRight);
    connect(InlineEditors::instance()->richTextJustified, &QAction::triggered, this, &HtmlEditor::setBlock);

    //  InlineEditors::instance()->richTextToolBar()->show();
    cursorPositionChanged();
    charFormatChanged(textEdit->currentCharFormat());
    // QTimer::singleShot(0, this, &HtmlEditor::cursorPositionChanged);
    InlineEditors::instance()->enableRichTextToolBar();


    connect(textEdit, &QTextEdit::cursorPositionChanged, htmlContent->note()->basket(), &BasketScene::editorCursorPositionChanged);
    // In case it is a very big note, the top is displayed and Enter is pressed: the cursor is on bottom, we should enure it visible:
    QTimer::singleShot(0, htmlContent->note()->basket(), &BasketScene::editorCursorPositionChanged);
}

void HtmlEditor::cursorPositionChanged()
{
    {
        QSignalBlocker blocker(InlineEditors::instance()->richTextFont);
        InlineEditors::instance()->richTextFont->setCurrentFont(textEdit()->currentFont());
    }

    if (InlineEditors::instance()->richTextColor->color() != textEdit()->textColor())
        InlineEditors::instance()->richTextColor->setColor(textEdit()->textColor());
    InlineEditors::instance()->richTextBold->setChecked((textEdit()->fontWeight() >= QFont::Bold));
    InlineEditors::instance()->richTextItalic->setChecked(textEdit()->fontItalic());
    InlineEditors::instance()->richTextUnderline->setChecked(textEdit()->fontUnderline());

    switch (textEdit()->alignment()) {
    default:
    case 1 /*Qt::AlignLeft*/:
        InlineEditors::instance()->richTextLeft->setChecked(true);
        break;
    case 2 /*Qt::AlignRight*/:
        InlineEditors::instance()->richTextRight->setChecked(true);
        break;
    case 4 /*Qt::AlignHCenter*/:
        InlineEditors::instance()->richTextCenter->setChecked(true);
        break;
    case 8 /*Qt::AlignJustify*/:
        InlineEditors::instance()->richTextJustified->setChecked(true);
        break;
    }
}

void HtmlEditor::editTextChanged()
{
    // The following is a workaround for an apparent Qt bug.
    // When I start typing in a textEdit, the undo&redo actions are not enabled until I click
    // or move the cursor - probably, the signal undoAvailable() is not emitted.
    // So, I had to intervene and do that manually.
    InlineEditors::instance()->richTextUndo->setEnabled(textEdit()->document()->isUndoAvailable());
    InlineEditors::instance()->richTextRedo->setEnabled(textEdit()->document()->isRedoAvailable());
}

void HtmlEditor::charFormatChanged(const QTextCharFormat &format)
{
    InlineEditors::instance()->richTextFontSize->setFontSize(format.font().pointSize());

    const auto alignment = format.verticalAlignment();
    {
        QSignalBlocker blocker(InlineEditors::instance()->richTextSuper);
        InlineEditors::instance()->richTextSuper->setChecked(alignment == QTextCharFormat::AlignSuperScript);
    }
    {
        QSignalBlocker blocker(InlineEditors::instance()->richTextSub);
        InlineEditors::instance()->richTextSub->setChecked(alignment == QTextCharFormat::AlignSubScript);
    }
}

void HtmlEditor::setSuperscript(bool isChecked)
{
    auto *editor = dynamic_cast<FocusedTextEdit *>(textEdit());
    if (!editor)
        return;

    if (isChecked) {
        QSignalBlocker blocker(InlineEditors::instance()->richTextSub);
        InlineEditors::instance()->richTextSub->setChecked(false);
    }

    editor->applyVerticalAlignment(
        isChecked ? QTextCharFormat::AlignSuperScript : QTextCharFormat::AlignNormal);

    QTimer::singleShot(0, note()->basket(), &BasketScene::focusEditor);
}

void HtmlEditor::setSubscript(bool isChecked)
{
    auto *editor = dynamic_cast<FocusedTextEdit *>(textEdit());
    if (!editor)
        return;

    if (isChecked) {
        QSignalBlocker blocker(InlineEditors::instance()->richTextSuper);
        InlineEditors::instance()->richTextSuper->setChecked(false);
    }

    editor->applyVerticalAlignment(
        isChecked ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal);

    QTimer::singleShot(0, note()->basket(), &BasketScene::focusEditor);
}

void HtmlEditor::setLeft()
{
    textEdit()->setAlignment(Qt::AlignLeft);
}
void HtmlEditor::setRight()
{
    textEdit()->setAlignment(Qt::AlignRight);
}
void HtmlEditor::setCentered()
{
    textEdit()->setAlignment(Qt::AlignHCenter);
}
void HtmlEditor::setBlock()
{
    textEdit()->setAlignment(Qt::AlignJustify);
}

void HtmlEditor::onFontSelectionChanged(const QFont &font)
{
    if (auto *editor =
            dynamic_cast<FocusedTextEdit *>(textEdit())) {
        editor->applyFontFamily(font.family());
    } else {
        textEdit()->setFontFamily(font.family());
    }
}

void HtmlEditor::setBold(bool isChecked)
{
    qCWarning(BASKET_LOG) << "setBold " << isChecked;

    if (auto *editor =
            dynamic_cast<FocusedTextEdit *>(textEdit())) {
        editor->applyFontWeight(
            isChecked ? QFont::Bold : QFont::Normal);
    } else {
        textEdit()->setFontWeight(
            isChecked ? QFont::Bold : QFont::Normal);
    }
}

HtmlEditor::~HtmlEditor() = default;

void HtmlEditor::autoSave(bool toFileToo)
{
    m_htmlContent->setHtml(Tools::textDocumentToMinimalHTML(textEdit()->document()));
    if (toFileToo) {
        m_htmlContent->saveToFile();
        m_htmlContent->setEdited();
    }
}

void HtmlEditor::validate()
{
    if (Tools::htmlToText(textEdit()->toHtml()).isEmpty())
        setEmpty();
    if (Settings::detectTextTags())
        detectTags();
    QString convert = Tools::textDocumentToMinimalHTML(textEdit()->document());
    QString textEquivalent = Tools::htmlToText(convert);

    if (note()->allowCrossReferences())
        convert = Tools::detectCrossReferences(convert, /*userLink=*/true);

    m_htmlContent->setHtml(convert);
    m_htmlContent->saveToFile();
    m_htmlContent->setEdited();

    disconnect();
    graphicsWidget()->disconnect();
    if (InlineEditors::instance()) {
        InlineEditors::instance()->disableRichTextToolBar();
        //      if (InlineEditors::instance()->richTextToolBar())
        //          InlineEditors::instance()->richTextToolBar()->hide();
    }

    if (graphicsWidget()) {
        note()->setZValue(1);
        setInlineEditor(nullptr);
    }
}

void HtmlEditor::detectTags()
{
    QTextDocument *doc = textEdit()->document();
    const QTextBlock &block = doc->firstBlock();
    if (!block.isValid() || block.begin() == block.end())
        return;

    const QTextFragment &fragment = block.begin().fragment();
    if (!fragment.isValid() || fragment.length() == 0)
        return;
    // Process unstyled text only
    const QTextCharFormat &charFmt = fragment.charFormat();
    if (charFmt.propertyCount() > 0)
        return;

    QString newText = fragment.text();
    int prefixLength;
    QTextCursor cursor(block);
    const QList<State *> &states = Tools::detectTags(fragment.text(), prefixLength);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, prefixLength);
    cursor.removeSelectedText();

    for (State *state : states) {
        note()->addState(state, true);
    }
}
/** class ImageEditor: */

ImageEditor::ImageEditor(ImageContent *imageContent, QWidget *parent)
    : NoteEditor(imageContent)
{
    int choice = KMessageBox::questionTwoActionsCancel(
        parent,
        i18n("Images cannot be edited here at the moment.\n"
             "Do you want to open it with an application that understand it?"),
        i18n("Edit Image Mathom"),
        KStandardGuiItem::open(),
        KGuiItem(i18n("Load From &File..."), QStringLiteral("document-import")),
        KStandardGuiItem::cancel());

    switch (choice) {
    case (KMessageBox::Ok):
        note()->basket()->noteOpen(note());
        break;
    case (KMessageBox::SecondaryAction): // Load from file
        cancel();
        Global::bnpView->insertWizard(3); // 3 maps to m_actLoadFile
        break;
    case (KMessageBox::Cancel):
        cancel();
        break;
    }
}

/** class AnimationEditor: */

AnimationEditor::AnimationEditor(AnimationContent *animationContent, QWidget *parent)
    : NoteEditor(animationContent)
{
    int choice = KMessageBox::questionTwoActions(parent,
                                                 i18n("This animated image can not be edited here.\n"
                                                      "Do you want to open it with an application that understands it?"),
                                                 i18n("Edit Animation Mathom"),
                                                 KStandardGuiItem::open(),
                                                 KStandardGuiItem::cancel());

    if (choice == KMessageBox::PrimaryAction)
        note()->basket()->noteOpen(note());
}

/** class FileEditor: */

FileEditor::FileEditor(FileContent *fileContent, QWidget * /*parent*/)
    : NoteEditor(fileContent)
    , m_fileContent(fileContent)
{
    auto *lineEdit = new QLineEdit(nullptr);
    auto *filter = new FocusWidgetFilter(lineEdit);

    QPalette palette;
    palette.setColor(lineEdit->backgroundRole(), note()->backgroundColor());
    palette.setColor(lineEdit->foregroundRole(), note()->textColor());
    lineEdit->setPalette(palette);

    lineEdit->setFont(note()->font());
    lineEdit->setText(m_fileContent->fileName());
    lineEdit->selectAll();
    setInlineEditor(lineEdit);
    connect(filter, &FocusWidgetFilter::returnPressed, this, &FileEditor::askValidation);
    connect(filter, &FocusWidgetFilter::escapePressed, this, &FileEditor::askValidation);
    connect(filter, &FocusWidgetFilter::mouseEntered, this, &FileEditor::mouseEnteredEditorWidget);
}

FileEditor::~FileEditor() = default;

void FileEditor::autoSave(bool toFileToo)
{
    // FIXME: How to detect cancel?
    if (toFileToo && !lineEdit()->text().isEmpty() && m_fileContent->trySetFileName(lineEdit()->text())) {
        m_fileContent->setFileName(lineEdit()->text());
        m_fileContent->setEdited();
    }
}

void FileEditor::validate()
{
    autoSave(/*toFileToo=*/true);
}

/** class LinkEditor: */

LinkEditor::LinkEditor(LinkContent *linkContent, QWidget *parent)
    : NoteEditor(linkContent)
{
    QPointer<LinkEditDialog> dialog = new LinkEditDialog(linkContent, parent);
    if (dialog->exec() == QDialog::Rejected)
        cancel();
    if (linkContent->url().isEmpty() && linkContent->title().isEmpty())
        setEmpty();
}

/** class CrossReferenceEditor: */

CrossReferenceEditor::CrossReferenceEditor(CrossReferenceContent *crossReferenceContent, QWidget *parent)
    : NoteEditor(crossReferenceContent)
{
    QPointer<CrossReferenceEditDialog> dialog = new CrossReferenceEditDialog(crossReferenceContent, parent);
    if (dialog->exec() == QDialog::Rejected)
        cancel();
    if (crossReferenceContent->url().isEmpty() && crossReferenceContent->title().isEmpty())
        setEmpty();
}

/** class LauncherEditor: */

LauncherEditor::LauncherEditor(LauncherContent *launcherContent, QWidget *parent)
    : NoteEditor(launcherContent)
{
    QPointer<LauncherEditDialog> dialog = new LauncherEditDialog(launcherContent, parent);
    if (dialog->exec() == QDialog::Rejected)
        cancel();
    if (launcherContent->name().isEmpty() && launcherContent->exec().isEmpty())
        setEmpty();
}

/** class ColorEditor: */

ColorEditor::ColorEditor(ColorContent *colorContent, QWidget *parent)
    : NoteEditor(colorContent)
{
    const QColor oldColor = colorContent->color();
    const QColor result = QColorDialog::getColor(oldColor, parent, i18n("Edit Color Mathom"));
    if (result.isValid()) {
        if (result != oldColor) {
            colorContent->setColor(result);
            colorContent->setEdited();
        }
    } else
        cancel();
}

/** class UnknownEditor: */

UnknownEditor::UnknownEditor(UnknownContent *unknownContent, QWidget *parent)
    : NoteEditor(unknownContent)
{
    KMessageBox::information(parent,
                             i18n("The type of this mathom is unknown and cannot be edited here.\n"
                                  "You however can drag or copy the mathom into an application that understands it."),
                             i18n("Edit Unknown Mathom"));
}

/*********************************************************************/


/** class SpreadsheetEditor: */

SpreadsheetEditor::SpreadsheetEditor(SpreadsheetContent *spreadsheetContent, QWidget * /*parent*/)
    : NoteEditor(spreadsheetContent)
    , m_spreadsheetContent(spreadsheetContent)
    , m_table(new QTableWidget(spreadsheetContent->rowCount(), spreadsheetContent->columnCount()))
    , m_cellAddress(new QLabel())
    , m_formulaEdit(new QLineEdit())
    , m_functionCombo(new QComboBox())
{
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *tools = new QHBoxLayout();
    auto *addRow = new QPushButton(i18n("+ Row"), container);
    auto *removeRow = new QPushButton(i18n("- Row"), container);
    auto *addColumn = new QPushButton(i18n("+ Column"), container);
    auto *removeColumn = new QPushButton(i18n("- Column"), container);
    tools->addWidget(addRow);
    tools->addWidget(removeRow);
    tools->addWidget(addColumn);
    tools->addWidget(removeColumn);
    tools->addStretch();
    layout->addLayout(tools);

    auto *formulaBar = new QHBoxLayout();
    m_cellAddress->setMinimumWidth(48);
    m_cellAddress->setAlignment(Qt::AlignCenter);
    m_cellAddress->setText(QStringLiteral("A1"));

    m_functionCombo->addItem(i18n("Function..."), QString());
    m_functionCombo->addItem(i18n("Sum"), QStringLiteral("SUM"));
    m_functionCombo->addItem(i18n("Average"), QStringLiteral("AVERAGE"));
    m_functionCombo->addItem(i18n("Minimum"), QStringLiteral("MIN"));
    m_functionCombo->addItem(i18n("Maximum"), QStringLiteral("MAX"));
    m_functionCombo->addItem(i18n("Count"), QStringLiteral("COUNT"));

    m_formulaEdit->setPlaceholderText(i18n("Value or formula, for example =A1+B1"));
    auto *applyFormula = new QPushButton(i18n("Apply"), container);

    formulaBar->addWidget(m_cellAddress);
    formulaBar->addWidget(m_functionCombo);
    formulaBar->addWidget(m_formulaEdit, 1);
    formulaBar->addWidget(applyFormula);
    layout->addLayout(formulaBar);

    m_table->setParent(container);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(26);

    QStringList headers;
    for (int column = 0; column < spreadsheetContent->columnCount(); ++column)
        headers.append(SpreadsheetContent::columnName(column));
    m_table->setHorizontalHeaderLabels(headers);

    for (int row = 0; row < spreadsheetContent->rowCount(); ++row) {
        for (int column = 0; column < spreadsheetContent->columnCount(); ++column)
            m_table->setItem(row, column, new QTableWidgetItem(spreadsheetContent->cell(row, column)));
    }

    layout->addWidget(m_table);
    setInlineEditor(container);
    deferInlineWidgetDeletion();

    BasketScene *scene = spreadsheetContent->note()->basket();

    connect(m_table, &QTableWidget::itemChanged, scene, &BasketScene::contentChangedInEditor);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) {
        updateFormulaBar();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        updateFormulaBar();
    });

    connect(m_formulaEdit, &QLineEdit::returnPressed, this, &SpreadsheetEditor::commitFormulaBar);
    connect(applyFormula, &QPushButton::clicked, this, &SpreadsheetEditor::commitFormulaBar);
    connect(m_functionCombo, &QComboBox::activated, this, [this](int index) {
        const QString functionName = m_functionCombo->itemData(index).toString();
        if (!functionName.isEmpty())
            insertFunction(functionName);
        m_functionCombo->setCurrentIndex(0);
    });

    connect(addRow, &QPushButton::clicked, this, [this, scene]() {
        m_table->insertRow(m_table->rowCount());
        scene->contentChangedInEditor();
        updateFormulaBar();
    });
    connect(removeRow, &QPushButton::clicked, this, [this, scene]() {
        if (m_table->rowCount() > 1) {
            const int row = m_table->currentRow() >= 0 ? m_table->currentRow() : m_table->rowCount() - 1;
            m_table->removeRow(row);
            scene->contentChangedInEditor();
            updateFormulaBar();
        }
    });
    connect(addColumn, &QPushButton::clicked, this, [this, scene]() {
        const int column = m_table->columnCount();
        m_table->insertColumn(column);
        m_table->setHorizontalHeaderItem(column, new QTableWidgetItem(SpreadsheetContent::columnName(column)));
        scene->contentChangedInEditor();
        updateFormulaBar();
    });
    connect(removeColumn, &QPushButton::clicked, this, [this, scene]() {
        if (m_table->columnCount() > 1) {
            const int column = m_table->currentColumn() >= 0 ? m_table->currentColumn() : m_table->columnCount() - 1;
            m_table->removeColumn(column);
            for (int index = 0; index < m_table->columnCount(); ++index)
                m_table->setHorizontalHeaderItem(index, new QTableWidgetItem(SpreadsheetContent::columnName(index)));
            scene->contentChangedInEditor();
            updateFormulaBar();
        }
    });

    m_table->setCurrentCell(0, 0);
    updateFormulaBar();
    m_table->setFocus();
}

SpreadsheetEditor::~SpreadsheetEditor() = default;

void SpreadsheetEditor::ensureCellExists(int row, int column)
{
    if (row < 0 || column < 0 || row >= m_table->rowCount() || column >= m_table->columnCount())
        return;

    if (!m_table->item(row, column))
        m_table->setItem(row, column, new QTableWidgetItem());
}

void SpreadsheetEditor::updateFormulaBar()
{
    const int row = m_table->currentRow();
    const int column = m_table->currentColumn();

    if (row < 0 || column < 0) {
        m_cellAddress->setText(QString());
        m_formulaEdit->clear();
        return;
    }

    m_cellAddress->setText(SpreadsheetContent::columnName(column) + QString::number(row + 1));
    ensureCellExists(row, column);

    if (QTableWidgetItem *item = m_table->item(row, column))
        m_formulaEdit->setText(item->text());
}

void SpreadsheetEditor::commitFormulaBar()
{
    const int row = m_table->currentRow();
    const int column = m_table->currentColumn();
    if (row < 0 || column < 0)
        return;

    ensureCellExists(row, column);
    m_table->item(row, column)->setText(m_formulaEdit->text());
    m_table->setFocus();
}

void SpreadsheetEditor::insertFunction(const QString &functionName)
{
    const QList<QTableWidgetSelectionRange> ranges = m_table->selectedRanges();

    int targetRow = m_table->currentRow();
    int targetColumn = m_table->currentColumn();
    QString argument;

    if (!ranges.isEmpty()) {
        const QTableWidgetSelectionRange range = ranges.first();
        argument = SpreadsheetContent::columnName(range.leftColumn()) + QString::number(range.topRow() + 1)
            + QLatin1Char(':')
            + SpreadsheetContent::columnName(range.rightColumn()) + QString::number(range.bottomRow() + 1);

        if (range.rowCount() > 1 && range.columnCount() == 1) {
            targetRow = qMin(m_table->rowCount() - 1, range.bottomRow() + 1);
            targetColumn = range.leftColumn();
        } else if (range.columnCount() > 1 && range.rowCount() == 1) {
            targetRow = range.topRow();
            targetColumn = qMin(m_table->columnCount() - 1, range.rightColumn() + 1);
        }
    }

    if (argument.isEmpty() && targetRow >= 0 && targetColumn >= 0)
        argument = SpreadsheetContent::columnName(targetColumn) + QString::number(targetRow + 1);

    if (targetRow < 0 || targetColumn < 0)
        return;

    ensureCellExists(targetRow, targetColumn);
    const QString formula = QStringLiteral("=%1(%2)").arg(functionName, argument);
    m_table->setCurrentCell(targetRow, targetColumn);
    m_table->item(targetRow, targetColumn)->setText(formula);
    updateFormulaBar();
    m_table->setFocus();
}


void SpreadsheetEditor::syncContent(bool saveToFile)
{
    QVector<QVector<QString>> cells(m_table->rowCount(), QVector<QString>(m_table->columnCount()));

    for (int row = 0; row < m_table->rowCount(); ++row) {
        for (int column = 0; column < m_table->columnCount(); ++column) {
            if (QTableWidgetItem *item = m_table->item(row, column))
                cells[row][column] = item->text();
        }
    }

    m_spreadsheetContent->setTableData(m_table->rowCount(), m_table->columnCount(), cells);

    if (saveToFile) {
        m_spreadsheetContent->saveToFile();
        m_spreadsheetContent->setEdited();
    }
}

void SpreadsheetEditor::validate()
{
    syncContent(true);
}

void SpreadsheetEditor::autoSave(bool toFileToo)
{
    syncContent(toFileToo);
}

/** class LinkEditDialog: */

LinkEditDialog::LinkEditDialog(LinkContent *contentNote, QWidget *parent /*, QKeyEvent *ke*/)
    : QDialog(parent)
    , m_noteContent(contentNote)
{
    // QDialog options
    setWindowTitle(i18n("Edit Link Mathom"));
    setObjectName("EditLink");
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *layout = new QFormLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(layout);

    m_url = new KUrlRequester(this);
    m_url->setMode(KFile::File | KFile::ExistingOnly);
    m_url->lineEdit()->setMinimumWidth(m_url->lineEdit()->fontMetrics().maxWidth() * 20);
    layout->addRow(i18n("Ta&rget:"), m_url);

    auto *titleWidget = new QWidget(this);
    auto *titleLay = new QHBoxLayout(titleWidget);
    titleLay->setContentsMargins(0, 0, 0, 0);
    m_title = new QLineEdit(titleWidget);
    m_title->setMinimumWidth(m_title->fontMetrics().maxWidth() * 20);
    titleLay->addWidget(m_title);
    m_autoTitle = new QPushButton(i18n("Auto"), titleWidget);
    m_autoTitle->setCheckable(true);
    titleLay->addWidget(m_autoTitle);
    layout->addRow(i18n("&Title:"), titleWidget);

    auto *iconWidget = new QWidget(this);
    auto *iconLay = new QHBoxLayout(iconWidget);
    iconLay->setContentsMargins(0, 0, 0, 0);
    m_icon = new KIconButton(iconWidget);
    m_icon->setIconType(KIconLoader::NoGroup, KIconLoader::MimeType);
    iconLay->addWidget(m_icon);
    m_autoIcon = new QPushButton(i18n("Auto"), iconWidget);
    m_autoIcon->setCheckable(true);
    iconLay->addWidget(m_autoIcon);
    iconLay->addStretch();
    const int minSize = m_autoIcon->sizeHint().height();
    // Make the icon button at least the same height than the other buttons for a better alignment (nicer to the eyes):
    if (m_icon->sizeHint().height() < minSize)
        m_icon->setFixedSize(minSize, minSize);
    else
        m_icon->setFixedSize(m_icon->sizeHint().height(), m_icon->sizeHint().height()); // Make it square
    layout->addRow(i18n("&Icon:"), iconWidget);

    mainLayout->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    connect(okButton, &QAbstractButton::clicked, this, &LinkEditDialog::slotOk);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    if (!m_noteContent->url().isEmpty()) {
        m_url->setUrl(QUrl(m_noteContent->url().toDisplayString()));
    }
    m_icon->setIcon(m_noteContent->icon());
    m_title->setText(m_noteContent->title());
    m_autoTitle->setChecked(m_noteContent->autoTitle());
    m_autoIcon->setChecked(m_noteContent->autoIcon());
    const QUrl filteredURL = NoteFactory::filteredURL(QUrl::fromUserInput(m_url->lineEdit()->text()));
    m_icon->setIconSize(LinkLook::lookForURL(filteredURL)->iconSize());

    m_isAutoModified = false;
    connect(m_url, &KUrlRequester::textChanged, this, &LinkEditDialog::urlChanged);
    connect(m_title, &QLineEdit::textChanged, this, &LinkEditDialog::doNotAutoTitle);
    connect(m_icon, &KIconButton::iconChanged, this, &LinkEditDialog::doNotAutoIcon);
    connect(m_autoTitle, &QPushButton::clicked, this, [this](bool) {
        guessTitle();
    });
    connect(m_autoIcon, &QPushButton::clicked, this, [this](bool) {
        guessIcon();
    });
}

LinkEditDialog::~LinkEditDialog() = default;

bool LinkEditDialog::event(QEvent *event)
{
    const bool result = QDialog::event(event);
    if (event->type() == QEvent::Polish) {
        if (m_url->lineEdit()->text().isEmpty()) {
            m_url->setFocus();
            m_url->lineEdit()->end(false);
        } else {
            m_title->setFocus();
            m_title->end(false);
        }
    }
    return result;
}

void LinkEditDialog::urlChanged(const QString &)
{
    m_isAutoModified = true;
    //  guessTitle();
    //  guessIcon();
    // Optimization (filter only once):
    QUrl filteredURL = NoteFactory::filteredURL(m_url->url()); // KURIFilter::self()->filteredURI(KUrl(m_url->url()));
    if (m_autoIcon->isChecked())
        m_icon->setIcon(NoteFactory::iconForURL(filteredURL));
    if (m_autoTitle->isChecked()) {
        m_title->setText(NoteFactory::titleForURL(filteredURL));
        m_autoTitle->setChecked(true); // Because the setText() will disable it!
    }
}

void LinkEditDialog::doNotAutoTitle(const QString &)
{
    if (m_isAutoModified)
        m_isAutoModified = false;
    else
        m_autoTitle->setChecked(false);
}

void LinkEditDialog::doNotAutoIcon(QString)
{
    m_autoIcon->setChecked(false);
}

void LinkEditDialog::guessIcon()
{
    if (m_autoIcon->isChecked()) {
        QUrl filteredURL = NoteFactory::filteredURL(m_url->url()); // KURIFilter::self()->filteredURI(KUrl(m_url->url()));
        m_icon->setIcon(NoteFactory::iconForURL(filteredURL));
    }
}

void LinkEditDialog::guessTitle()
{
    if (m_autoTitle->isChecked()) {
        QUrl filteredURL = NoteFactory::filteredURL(m_url->url()); // KURIFilter::self()->filteredURI(KUrl(m_url->url()));
        m_title->setText(NoteFactory::titleForURL(filteredURL));
        m_autoTitle->setChecked(true); // Because the setText() will disable it!
    }
}

void LinkEditDialog::slotOk()
{
    QUrl filteredURL = NoteFactory::filteredURL(m_url->url()); // KURIFilter::self()->filteredURI(KUrl(m_url->url()));
    m_noteContent->setLink(filteredURL, m_title->text(), m_icon->icon(), m_autoTitle->isChecked(), m_autoIcon->isChecked());
    m_noteContent->setEdited();

    /* Change icon size if link look have changed */
    LinkLook *linkLook = LinkLook::lookForURL(filteredURL);
    QString icon = m_icon->icon(); // When we change size, icon isn't changed and keep it's old size
    m_icon->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum); // Reset size policy
    m_icon->setIconSize(linkLook->iconSize()); //  So I store it's name and reload it after size change !
    m_icon->setIcon(icon);
    int minSize = m_autoIcon->sizeHint().height();
    // Make the icon button at least the same height than the other buttons for a better alignment (nicer to the eyes):
    if (m_icon->sizeHint().height() < minSize)
        m_icon->setFixedSize(minSize, minSize);
    else
        m_icon->setFixedSize(m_icon->sizeHint().height(), m_icon->sizeHint().height()); // Make it square
}

/** class CrossReferenceEditDialog: */

CrossReferenceEditDialog::CrossReferenceEditDialog(CrossReferenceContent *contentNote, QWidget *parent /*, QKeyEvent *ke*/)
    : QDialog(parent)
    , m_noteContent(contentNote)
{
    // QDialog options
    setWindowTitle(i18n("Edit Cross Reference"));
    setObjectName("EditCrossReference");
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *layout = new QFormLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(layout);

    m_targetBasket = new KComboBox(this);
    layout->addRow(i18n("Ta&rget:"), m_targetBasket);

    mainLayout->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(okButton, &QAbstractButton::clicked, this, &CrossReferenceEditDialog::slotOk);
    mainLayout->addWidget(buttonBox);

    generateBasketList(m_targetBasket);
    if (m_noteContent->url().isEmpty()) {
        BasketListViewItem *item = Global::bnpView->topLevelItem(0);
        m_noteContent->setCrossReference(QUrl::fromUserInput(item->data(0, Qt::UserRole).toString()),
                                         m_targetBasket->currentText(),
                                         QStringLiteral("edit-copy"));
        urlChanged(0);
    } else {
        QString url = m_noteContent->url().url();
        // cannot use findData because I'm using a StringList and I don't have the second
        // piece of data to make find work.
        for (int i = 0; i < m_targetBasket->count(); ++i) {
            if (url == m_targetBasket->itemData(i, Qt::UserRole).toStringList().first()) {
                m_targetBasket->setCurrentIndex(i);
                break;
            }
        }
    }

    connect(m_targetBasket, &QComboBox::activated, this, &CrossReferenceEditDialog::urlChanged);
}

CrossReferenceEditDialog::~CrossReferenceEditDialog() = default;

void CrossReferenceEditDialog::urlChanged(const int index)
{
    if (m_targetBasket)
        m_noteContent->setCrossReference(QUrl::fromUserInput(m_targetBasket->itemData(index, Qt::UserRole).toStringList().first()),
                                         m_targetBasket->currentText().trimmed(),
                                         m_targetBasket->itemData(index, Qt::UserRole).toStringList().last());
}

void CrossReferenceEditDialog::slotOk()
{
    m_noteContent->setEdited();
}

void CrossReferenceEditDialog::generateBasketList(KComboBox *targetList, BasketListViewItem *item, int indent)
{
    if (!item) { // include ALL top level items and their children.
        for (int i = 0; i < Global::bnpView->topLevelItemCount(); ++i)
            this->generateBasketList(targetList, Global::bnpView->topLevelItem(i));
    } else {
        BasketScene *bv = item->basket();

        // TODO: add some fancy deco stuff to make it look like a tree list.
        QString pad;
        QString text = item->text(0); // user text

        text.prepend(pad.fill(QLatin1Char(' '), indent * 2));

        // create the link text
        QString link = QStringLiteral("basket://");
        link.append(bv->folderName().toLower()); // unique ref.
        QStringList data;
        data.append(link);
        data.append(bv->icon());

        targetList->addItem(item->icon(0), text, QVariant(data));

        int subBasketCount = item->childCount();
        if (subBasketCount > 0) {
            indent++;
            for (int i = 0; i < subBasketCount; ++i) {
                this->generateBasketList(targetList, (BasketListViewItem *)item->child(i), indent);
            }
        }
    }
}

/** class LauncherEditDialog: */

LauncherEditDialog::LauncherEditDialog(LauncherContent *contentNote, QWidget *parent)
    : QDialog(parent)
    , m_noteContent(contentNote)
{
    // QDialog options
    setWindowTitle(i18n("Edit Launcher Mathom"));
    setObjectName("EditLauncher");
    setModal(true);

    const KService service(contentNote->fullPath());

    auto *mainLayout = new QVBoxLayout(this);

    auto *layout = new QFormLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(layout);

    m_command = new RunCommandRequester(service.exec(), i18n("Choose a command to run:"), this);
    layout->addRow(i18n("Comman&d:"), m_command);

    m_name = new QLineEdit(service.name(), this);
    layout->addRow(i18n("&Name:"), m_name);

    auto *iconWidget = new QWidget(this);
    auto *iconLay = new QHBoxLayout(iconWidget);
    iconLay->setContentsMargins(0, 0, 0, 0);
    m_icon = new KIconButton(iconWidget);
    m_icon->setIconType(KIconLoader::NoGroup, KIconLoader::Application);
    iconLay->addWidget(m_icon);
    auto *guessButton = new QPushButton(i18n("&Guess"), this);
    iconLay->addWidget(guessButton);
    iconLay->addStretch();
    int minSize = guessButton->sizeHint().height();
    // Make the icon button at least the same height than the other buttons for a better alignment (nicer to the eyes):
    if (m_icon->sizeHint().height() < minSize)
        m_icon->setFixedSize(minSize, minSize);
    else
        m_icon->setFixedSize(m_icon->sizeHint().height(), m_icon->sizeHint().height()); // Make it square
    layout->addRow(i18n("&Icon:"), iconWidget);

    mainLayout->addStretch();

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    connect(okButton, &QAbstractButton::clicked, this, &LauncherEditDialog::slotOk);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    m_icon->setIcon(service.icon());

    connect(guessButton, &QAbstractButton::clicked, this, &LauncherEditDialog::guessIcon);
}

LauncherEditDialog::~LauncherEditDialog() = default;

bool LauncherEditDialog::event(QEvent *event)
{
    const bool result = QDialog::event(event);
    if (event->type() == QEvent::Polish) {
        if (m_command->runCommand().isEmpty()) {
            m_command->lineEdit()->setFocus();
        } else {
            m_name->setFocus();
            m_name->end(false);
        }
    }
    return result;
}

void LauncherEditDialog::slotOk()
{
    // TODO: Remember if a string has been modified AND IS DIFFERENT FROM THE
    // ORIGINAL!

    const QString command =
        Tools::launcherCommandWithoutFieldCodes(
            m_command->runCommand());

    KDesktopFile dtFile(m_noteContent->fullPath());
    KConfigGroup grp = dtFile.desktopGroup();
    grp.writeEntry("Exec", command);
    grp.writeEntry("Name", m_name->text());
    grp.writeEntry("Icon", m_icon->icon());

    // Just for faster feedback: conf object will save to disk (and then
    // m_note->loadContent() called)
    m_noteContent->setLauncher(
        m_name->text(),
        m_icon->icon(),
        command);
    m_noteContent->setEdited();
}

void LauncherEditDialog::guessIcon()
{
    m_icon->setIcon(NoteFactory::iconForCommand(m_command->runCommand()));
}

/** class InlineEditors: */

InlineEditors::InlineEditors() = default;

InlineEditors::~InlineEditors() = default;

InlineEditors *InlineEditors::instance()
{
    static InlineEditors *instance = nullptr;
    if (!instance)
        instance = new InlineEditors();
    return instance;
}

void InlineEditors::initToolBars(KActionCollection *ac)
{
    QFont defaultFont;
    QColor textColor = (Global::bnpView && Global::bnpView->currentBasket() ? Global::bnpView->currentBasket()->textColor() : palette().color(QPalette::Text));

    // Init the RichTextEditor Toolbar:
    richTextFont = new QFontComboBox();
    focusWidgetFilter = new FocusWidgetFilter(richTextFont);
    richTextFont->setFixedWidth(richTextFont->sizeHint().width() * 2 / 3);
    richTextFont->setCurrentFont(defaultFont.family());

    // The font selector is a formatting control, not a text-entry field.
    // It must never capture typing intended for the active Mathom.
    richTextFont->setFocusPolicy(Qt::NoFocus);
    if (richTextFont->lineEdit()) {
        richTextFont->lineEdit()->setReadOnly(true);
        richTextFont->lineEdit()->setFocusPolicy(Qt::NoFocus);
    }

    auto *action = new QWidgetAction(ac);
    ac->addAction(QStringLiteral("richtext_font"), action);
    action->setDefaultWidget(richTextFont);
    action->setText(i18n("Font"));
    ac->setDefaultShortcut(action, Qt::Key_F6);

    richTextFontSize = new FontSizeCombo(/*rw=*/true, /*withDefault=*/false);
    richTextFontSize->setFontSize(defaultFont.pointSize());

    // Keep the editable display capability for arbitrary existing sizes,
    // but prevent the toolbar field from receiving keyboard input.
    richTextFontSize->setFocusPolicy(Qt::NoFocus);
    if (richTextFontSize->lineEdit()) {
        richTextFontSize->lineEdit()->setReadOnly(true);
        richTextFontSize->lineEdit()->setFocusPolicy(Qt::NoFocus);
    }
    action = new QWidgetAction(ac);
    ac->addAction(QStringLiteral("richtext_font_size"), action);
    action->setDefaultWidget(richTextFontSize);
    action->setText(i18n("Font Size"));
    ac->setDefaultShortcut(action, Qt::Key_F7);

    richTextColor = new KColorCombo();
    richTextColor->installEventFilter(focusWidgetFilter);
    richTextColor->setFixedWidth(richTextColor->sizeHint().height() * 2);
    richTextColor->setColor(textColor);

    // The color selector is a formatting control, not a text-entry field.
    // Do not let it capture typing intended for the active Mathom.
    richTextColor->setFocusPolicy(Qt::NoFocus);
    if (richTextColor->lineEdit()) {
        richTextColor->lineEdit()->setReadOnly(true);
        richTextColor->lineEdit()->setFocusPolicy(Qt::NoFocus);
    }
    action = new QWidgetAction(ac);
    ac->addAction(QStringLiteral("richtext_color"), action);
    action->setDefaultWidget(richTextColor);
    action->setText(i18n("Color"));

    KToggleAction *ta = nullptr;
    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_bold"), ta);
    ta->setText(i18n("Bold"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-text-bold")));
    ac->setDefaultShortcut(ta, QKeySequence(Qt::CTRL | Qt::Key_B));
    richTextBold = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_italic"), ta);
    ta->setText(i18n("Italic"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-text-italic")));
    ac->setDefaultShortcut(ta, QKeySequence(Qt::CTRL | Qt::Key_I));
    richTextItalic = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_underline"), ta);
    ta->setText(i18n("Underline"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-text-underline")));
    ac->setDefaultShortcut(ta, QKeySequence(Qt::CTRL | Qt::Key_U));
    richTextUnderline = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_super"), ta);
    ta->setText(i18n("Superscript"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-text-superscript")));
    richTextSuper = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_sub"), ta);
    ta->setText(i18n("Subscript"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-text-subscript")));
    richTextSub = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_left"), ta);
    ta->setText(i18n("Align Left"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-justify-left")));
    richTextLeft = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_center"), ta);
    ta->setText(i18n("Centered"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-justify-center")));
    richTextCenter = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_right"), ta);
    ta->setText(i18n("Align Right"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-justify-right")));
    richTextRight = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_block"), ta);
    ta->setText(i18n("Justified"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("format-justify-fill")));
    richTextJustified = ta;

    auto *alignmentGroup = new QActionGroup(ac);
    alignmentGroup->addAction(richTextLeft);
    alignmentGroup->addAction(richTextCenter);
    alignmentGroup->addAction(richTextRight);
    alignmentGroup->addAction(richTextJustified);

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_undo"), ta);
    ta->setText(i18n("Undo"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("edit-undo")));
    richTextUndo = ta;

    ta = new KToggleAction(ac);
    ac->addAction(QStringLiteral("richtext_redo"), ta);
    ta->setText(i18n("Redo"));
    ta->setIcon(MathomIcons::icon(QStringLiteral("edit-redo")));
    richTextRedo = ta;

    disableRichTextToolBar();
}

KToolBar *InlineEditors::richTextToolBar()
{
    if (Global::activeMainWindow()) {
        Global::activeMainWindow()->toolBar(); // Make sure we create the main toolbar FIRST, so it will be on top of the edit toolbar!
        return Global::activeMainWindow()->toolBar(QStringLiteral("richTextEditToolBar"));
    } else
        return nullptr;
}

void InlineEditors::enableRichTextToolBar()
{
    richTextFont->setEnabled(true);
    richTextFontSize->setEnabled(true);
    richTextColor->setEnabled(true);
    richTextBold->setEnabled(true);
    richTextItalic->setEnabled(true);
    richTextUnderline->setEnabled(true);
    richTextSuper->setEnabled(true);
    richTextSub->setEnabled(true);
    richTextLeft->setEnabled(true);
    richTextCenter->setEnabled(true);
    richTextRight->setEnabled(true);
    richTextJustified->setEnabled(true);
}

void InlineEditors::disableRichTextToolBar()
{
    disconnect(richTextFont);
    disconnect(richTextFontSize);
    disconnect(richTextColor);
    disconnect(richTextBold);
    disconnect(richTextItalic);
    disconnect(richTextUnderline);
    disconnect(richTextSuper);
    disconnect(richTextSub);
    disconnect(richTextLeft);
    disconnect(richTextCenter);
    disconnect(richTextRight);
    disconnect(richTextJustified);

    richTextFont->setEnabled(false);
    richTextFontSize->setEnabled(false);
    richTextColor->setEnabled(false);
    richTextBold->setEnabled(false);
    richTextItalic->setEnabled(false);
    richTextUnderline->setEnabled(false);
    richTextSuper->setEnabled(false);
    richTextSub->setEnabled(false);
    richTextLeft->setEnabled(false);
    richTextCenter->setEnabled(false);
    richTextRight->setEnabled(false);
    richTextJustified->setEnabled(false);

    // Return to a "proper" state:
    QFont defaultFont;
    QColor textColor = (Global::bnpView && Global::bnpView->currentBasket() ? Global::bnpView->currentBasket()->textColor() : palette().color(QPalette::Text));
    richTextFont->setCurrentFont(defaultFont.family());
    richTextFontSize->setFontSize(defaultFont.pointSize());
    richTextColor->setColor(textColor);
    richTextBold->setChecked(false);
    richTextItalic->setChecked(false);
    richTextUnderline->setChecked(false);
    richTextSuper->setChecked(false);
    richTextSub->setChecked(false);
    richTextLeft->setChecked(false);
    richTextCenter->setChecked(false);
    richTextRight->setChecked(false);
    richTextJustified->setChecked(false);
}

QPalette InlineEditors::palette() const
{
    return qApp->palette();
}

#include "moc_noteedit.cpp"
