#include <string>
#include <utility>

#include <QMessageBox>
#include <QInputDialog>

#include "chatwidget.hpp"

#include "cppa/cppa.hpp"
#include "cppa/util/scope_guard.hpp"

using namespace std;
using namespace cppa;

ChatWidget::ChatWidget(QWidget* parent, Qt::WindowFlags f)
: super(parent, f), m_input(nullptr), m_output(nullptr) {
    set_message_handler (
        on(atom("join"), arg_match) >> [=](const group_ptr& what) {
            if (m_chatroom) {
                send(m_chatroom, m_name + " has left the chatroom");
                self->leave(m_chatroom);
            }
            self->join(what);
            print(("*** joined " + to_string(what)).c_str());
            m_chatroom = what;
            send(what, m_name + " has entered the chatroom");
        },
        on(atom("setName"), arg_match) >> [=](string& name) {
            send(m_chatroom, m_name + " is now known as " + name);
            m_name = std::move(name);
            print("*** changed name to "
                              + QString::fromUtf8(m_name.c_str()));
        },
        on(atom("quit")) >> [=] {
            close(); // close widget
        },
        on<string>() >> [=](const string& txt) {
            // don't print own messages
            if (self != self->last_sender()) {
                print(QString::fromUtf8(txt.c_str()));
            }
        }
    );
}

void ChatWidget::sendChatMessage() {
    auto cleanup = util::make_scope_guard([=] {
        input()->setText(QString());
    });
    QString line = input()->text();
    if (line.startsWith('/')) {
        match_split(line.midRef(1).toUtf8().constData(), ' ') (
            on("join", arg_match) >> [=](const string& mod, const string& g) {
                group_ptr gptr;
                try { gptr = group::get(mod, g); }
                catch (exception& e) {
                    print("*** exception: " + QString::fromUtf8((e.what())));
                }
                if (gptr != nullptr) {
                    send_as(as_actor(), as_actor(), atom("join"), gptr);
                }
            },
            on("setName", arg_match) >> [=](const string& str) {
                send_as(as_actor(), as_actor(), atom("setName"), str);
            },
            others() >> [=] {
                print("*** list of commands:\n"
                      "/join <module> <group id>\n"
                      "/setName <new name>\n");
            }
        );
        return;
    }
    if (m_name.empty()) {
        print("*** please set a name before sending messages");
        return;
    }
    if (m_chatroom == nullptr) {
        print("*** no one is listening... please join a group");
        return;
    }
    string msg = m_name;
    msg += ": ";
    msg += line.toUtf8().constData();
    print("<you>: " + input()->text());
    // NOTE: we have to use send_as(as_actor(), ...) outside of our
    //       message handler, because `self` is *not* set properly
    //       in this context
    send_as(as_actor(), m_chatroom, std::move(msg));
}

void ChatWidget::joinGroup() {
    if (m_name.empty()) {
        QMessageBox::information(this,
                                 "No Name, No Chat",
                                 "Please set a name first.");
        return;
    }
    auto gname = QInputDialog::getText(this,
                                       "Join Group",
                                       "Please enter a group as <module>:<id>",
                                       QLineEdit::Normal,
                                       "remote:chatroom@localhost:4242");
    int pos = gname.indexOf(':');
    if (pos < 0) {
        QMessageBox::warning(this, "Not a Group", "Invalid format");
        return;
    }
    string mod = gname.left(pos).toUtf8().constData();
    string gid = gname.midRef(pos+1).toUtf8().constData();
    group_ptr gptr;
    try {
        auto gptr = group::get(mod, gid);
        send_as(as_actor(), as_actor(), atom("join"), gptr);
    }
    catch (exception& e) {
        QMessageBox::critical(this, "Exception", e.what());
    }
}

void ChatWidget::changeName() {
    auto name = QInputDialog::getText(this, "Change Name", "Please enter a new name");
    if (!name.isEmpty()) {
        send_as(as_actor(), as_actor(), atom("setName"), name.toUtf8().constData());
    }
}
