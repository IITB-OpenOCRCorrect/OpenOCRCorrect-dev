/*!
 * /class Project.cpp
 * /brief It is used for git functionality and set configuration for project.xml file.
 */
#include "Project.h"
#include "TreeItem.h"
#include "TreeModel.h"
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <sstream>
#include <ostream>
#include <iostream>
#include <pugixml.hpp>
#include <QDialog>
#include <QInputDialog>
#include "lg2_common.h"
#include "loadingspinner.h"
#include "qlabel.h"
#include "qnetworkaccessmanager.h"
#include "qnetworkreply.h"
#include "qprogressbar.h"
#include <QObject>
#include <git2.h>
#include <QProcess>
#include <QMessageBox>
#include <QDebug>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonObject>

QString user_id;
std::string user, pass, email;

bool takeCredentialsFromUser();

/*!
 * \fn Project::parse_project_xml
 * \brief Parse the xml file of project
 * \param pDoc
 */
void Project::parse_project_xml(rapidxml::xml_document<>& pDoc)
{

}

/*!
 * \fn Project::FindFile
 * \brief This function searches for the passed file name and returns the filename and its next consecutive file name from project.xml.
 * \details
 * This is a recursive function wherein We keep traversing the child notes of the directory tree
 * until we find the file. If the key(i.e the name of the child node file) matches the filename
 * the search is over and we return, if there are not child node files/directories then also we return.
 *
 * At some point once the function gets to the end of the tree in case the file is not found till yet
 * then there will be no children and thus it will return.
 *
 * \param file
 * \param QFile & file,pugi::xml_node  & n
 * \return FindFile(file, next)
 */
pugi::xml_node Project::FindFile(QFile & file,pugi::xml_node  & n)
{
    auto ch = n.child("File");
    if (!ch) return ch;
    auto key =  mProjectDir.relativeFilePath(file.fileName()).toStdString();
    while (ch)
    {
        std::string filename = ch.attribute("Include").as_string();    // Include attribute contains the file name in xml
        if (filename == key)
        {
            return ch;
        }
        ch = ch.next_sibling();
    }
    auto next = n.next_sibling();
    return FindFile(file, next);
}

/*!
 * \fn Project::set_stage_verifier
 * \brief Updates the stage value in xml file to 'Verifier'
 * \sa save_xml()
 */
void Project::set_stage_verifier()
{
    auto c = doc.child("Project").child("Metadata");
    bool s = c.child("Stage").first_child().set_value("Verifier");
    save_xml();
}

/*!
 * \fn Project::set_stage
 * \brief According to the pipeline once the project has been corrected it goes to verifier for further
 * verification.
 * \details
 * As such we update the XML of the project (project.xml file) and set the stage of the project to indicate
 * that it is in verification mode.
 *
 * \param mRole
 */
void Project::set_stage(QString mRole){
    std::string role = mRole.toUtf8().constData();
    auto c = doc.child("Project").child("Metadata");
    c.child("Stage").first_child().set_value(role.c_str());
    save_xml();
}

/*!
 * \fn Project::enable_push
 * \brief Increments the version value by one if passed value is true
 * and sets stage value as corrector in xml file.
 * We call this function to update the version of the project in the xml file.
 *
 * \param boolean
 * \sa save_xml()
 * \return bool
 */
bool Project::enable_push(bool increment)
{
    auto c = doc.child("Project").child("Metadata");
    bool s = c.child("Stage").first_child().set_value("Corrector");
    int ver = std::stoi(c.child("Version").child_value());
    if(increment)
        ver++;
    c.child("Version").first_child().set_value(std::to_string(ver).c_str());
    save_xml();
    return true;
}

/*!
 * \fn Project::set_version
 * \brief This function is to set version as passed in the parameters.
 * \param int ver
 */
void Project::set_version(int ver)
{
    auto c = doc.child("Project").child("Metadata");
    c.child("Version").first_child().set_value(std::to_string(ver).c_str());
    save_xml();
}

/*!
 * \fn Project::set_configuration
 * \brief We set the configuration of the project by passing the whole string of the configuration as a string
 * We match the xml tags with that of the configuration and change the InnerXML accordingly.
 * \param QString val
 */
void Project::set_configuration(QString val)
{
    std::string value = val.toUtf8().constData();
    auto c = doc.child("Project").child("Configuration");
    c.child("Prefixmatch").first_child().set_value(value.c_str());
    save_xml();
}

/*!
 * \fn Project::removeFile
 * \brief Removes the file name from project.xml and the hierarchial project tree view in ui window
 * \param idx
 * \param pFilter
 * \param pFile
 */
void Project::removeFile(QModelIndex & idx,Filter & pFilter, QFile & pFile)
{
    auto first = doc.child("Project").child("ItemGroup");

    auto ch = first.next_sibling();
    auto node = FindFile(pFile, ch);
    auto pnode  = node.parent();
    pnode.remove_child(node);
    save_xml();

    mTreeModel->layoutAboutToBeChanged();
    TreeItem * item = (TreeItem*)idx.internalPointer();
    auto parent = item->parentItem();
    mTreeModel->RemoveRow(idx.row(), 1, idx.parent());
    parent->RemoveNode(item);

    delete item;

    mTreeModel->layoutChanged();
}

/*!
 * \fn Project::process_node
 * \brief Here we process the nodes of the xml file by recursive iteration. Refer inline comments for more info.
 * \param pNode
 * \param parent
 */
void Project::process_node(pugi::xml_node * pNode, TreeItem * parent)
{

    if (pNode) {


        //! If the name of the node is "ItemGroup" then we get the child name and recursively traverse it
        std::string node_name = pNode->name();
        if (node_name == "ItemGroup")
        {
            pugi::xml_node p = pNode->first_child();
            process_node(&p, parent);
        }

        /*! If the name of the node is "Filter" then we get the name of the filter, if this node has no child then
         *  we set the setProjectOpen flag to false, wherein the project will not get opened even if we try to open it.
         *
         *  We push the filter into class variable mFilters and also create a new treeItem object with the
         *  parent.
         *
         *  We then append the node to the parent as a child and we recursively traverse child nodes.
         *
         */
        else if (node_name == "Filter")
        {
            /*   Example
                 <Filter Include="Image">
                    <Extensions>jpeg;jpg;png;</Extensions>
                 </Filter>
            */

            std::string filter_name = pNode->attribute("Include").as_string();;
            auto ch = pNode->child("Extensions");
            if(!ch) setProjectOpen(false);
            std::string filter_exts =  ch.child_value();

            Filter *filter = new Filter(filter_name, filter_exts);
            mFilters.push_back(filter);
            QString str;
            str = str.fromStdString(filter_name);
            //mFilters.push_back(filter_name);
            TreeItem * filter_node = new TreeItem(str, NodeType::FILTER, parent);
            filter_node->SetFilter(filter);
            parent->append_child(filter_node);
            auto p = pNode->next_sibling();
            process_node(&p, parent);
        }

        /*! If the name of the node is "File" then we do filter processing and scan through the files and add it
         *  to the tree item.
         *
         *  We append the nodefile to the parent as a child and recursively traverse the children
         *
         */

        else if (node_name == "File")
        {
            /*
                Example:
                <File Include="page-1.jpeg">
                    <Filter>Image</Filter>
                </File>
            */
            std::string filtername = pNode->child("Filter").child_value();
            QString qfilter;
            qfilter = qfilter.fromStdString(filtername);
            auto node = mRoot->find(qfilter);
            QString filepath = QString(pNode->attribute("Include").value());

            QString fpath = "";
            fpath = mProjectDir.absolutePath()+ "/" + filepath;
            QFileInfo fileinfo(fpath);
            QFile *f = new QFile(fpath);
            mFiles.push_back(f);
            auto filename = fileinfo.fileName();
            TreeItem *nodefile = new TreeItem(filename, _FILETYPE, node);
            node->append_child(nodefile);
            nodefile->SetFile(f);
            auto p = pNode->next_sibling();
            process_node(&p, parent);
        }
        /*! If the name of the node is "Metadata" then we check if the stage is corrector or verifier. If none
         *  of the condition holds then we set the flag setOpenProject to false to make the project unopenable.
         *
         */
        else if (node_name == "Metadata")
        {
            QString stage = pNode->child("Stage").child_value();
            if (stage != "Corrector" && stage != "Verifier")
                setProjectOpen(false);
        }
    }
    else {
        setProjectOpen(false); // otherwise we set project open to true
    }
}

/*!
 * \fn Project::process_xml
 * \brief This function proecesses the xml file and creates a tree model.
 * \param pFile
 * \details Give XML file path and the content will be stored inside the class and processed by rapidxml
 */
void Project::process_xml(QFile & pFile)
{
    if (mTreeModel)delete mTreeModel;
    /*if (mRoot) delete mRoot;*/
    setProjectOpen(true);
    pFile.open(QIODevice::ReadOnly);
    QFileInfo info;
    info.setFile(pFile);
    std::string path = pFile.fileName().toStdString();
    pugi::xml_parse_result res =  doc.load_file(path.c_str());
    if (!res) setProjectOpen(false);
    mProjectDir = info.absoluteDir();
    std::string dirName=(mProjectDir.dirName()).toStdString();

    mFileName = info.absoluteFilePath();
    mXML = pFile.readAll().toStdString();
    auto child = doc.child("Project");
    if (!child) setProjectOpen(false);
    child.attribute("name").set_value(dirName.c_str());
    save_xml();

    mProjectName = child.attribute("name").as_string();
    TreeItem * root = new TreeItem(mProjectName,FOLDER);

    mRoot = root;
    for (pugi::xml_node child : doc.child("Project")) {
        process_node(&child, root);
    }
    if(isProjectOpen()) {
        mTreeModel = new TreeModel();
        mTreeModel->setRoot(root);
        mTreeModel->layoutChanged();
    }
}


/*!
 * \fn Project::GetDir
 * \brief returns the directory of the project when called.
 */
QDir Project::GetDir()
{
    return mProjectDir;
}


/*!
 * \fn Project::addFile
 * \brief This function will add file to our project.
 * \details The function first traverses the tree and when the appropriate branch is reached the node is added to the parent
 * \param f
 * \param pFile
 */
void Project::addFile(Filter &f,QFile & pFile)
{
    auto node = doc.child("Project").child("ItemGroup").next_sibling();
    QString path = pFile.fileName();
    QString relpath = mProjectDir.relativeFilePath(path);
    std::string str = relpath.toStdString();
    std::string filtername = f.name().toStdString();
    /*
                Example:
                <File Include="page-1.jpeg">
                    <Filter>Image</Filter>
                </File>
    */

    pugi::xml_node chnode = node.append_child("File");
    chnode.append_attribute("Include") = str.c_str();
    auto fn = chnode.append_child("Filter").append_child(pugi::node_pcdata).set_value(filtername.c_str());
    auto val = chnode.first_child().child_value();
    save_xml();
    process_node(&chnode, mRoot);
    mTreeModel->layoutChanged();
}


/*!
 * \fn Project::AddTemp
 * \brief Adds only text files and html files to the project tree view whenever project is opened.
 * \param filter
 * \param file
 * \param prefix
 */
void Project::AddTemp(Filter * filter, QFile & file,QString prefix) {
    QString name = filter->name();
    TreeItem * t = mRoot->find(name);
    QFileInfo finfo(file.fileName());

    // Add only required files(ingore all except html and txt) to the tree view
    QString suff = finfo.completeSuffix();
    if (name == "Document" && suff != "txt" && suff != "html")
    {
        return ;
    }

    QString fileName = prefix+finfo.fileName();
    TreeItem * f = new TreeItem(fileName,NodeType::_FILETYPE,t);
    QFile * filep = new QFile(file.fileName());
    mFiles.push_back(filep);
    f->SetFile(filep);
    Filter * filtr = new Filter();
    f->SetFilter(filter);
    t->append_child(f);
    mTreeModel->layoutChanged();
}

/*!
 * \fn Project::save_xml
 * \brief This function when called saves the xml changes to disk. We used standard c++ functions to achieve this.
 */
void Project::save_xml()
{
    try
    {
        std::string str = mFileName.toStdString();
        bool isSaved = doc.save_file(str.c_str());
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "Runtime error was: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error was: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "An unknown error occurred." << std::endl;
    }
}

/*!
 * \fn Project::getFile
 * \brief Gets the file with filename
 * \param pFileName
 */
void Project::getFile(const QString & pFileName)
{
}

/*!
 * \fn Project::getModel
 * \brief returns the whole tree model whenever the function is called
 * \return TreeModel *model
 */
TreeModel * Project::getModel()
{
    return mTreeModel;
}

/*!
 * \fn make_opts
 * \brief Used to set Git options. Refer to git docs for more information
 * \param bool bare, const char * templ, uint32_t shared,
 * \return git_repository_init_options
 */
git_repository_init_options make_opts(bool bare, const char * templ,
                                      uint32_t shared,
                                      const char * gitdir,
                                      const char * dir)
{
    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;

    if (bare)
        opts.flags |= GIT_REPOSITORY_INIT_BARE;

    if (templ)
    {
        opts.flags |= GIT_REPOSITORY_INIT_EXTERNAL_TEMPLATE;
        opts.template_path = templ;
    }

    if (gitdir)
    {
        /* if you specified a separate git directory, then initialize
         * the repository at that path and use the second path as the
         * working directory of the repository (with a git-link file)
         */
        opts.workdir_path = dir;
        dir = gitdir;
    }

    if (shared != 0)
        opts.mode = shared;

    return opts;
}

/*!
 * \fn create_initial_commit
 * \brief This function is used to created the initial commit.
 *
 * We use lib git functions (a git library) to work with git and github.
 *
 * Check lib git library documentation for more info.
 *
 * \param repo
 */
void create_initial_commit(git_repository * repo) {
    git_signature *sig;
    git_index *index;
    git_oid tree_id, commit_id;
    git_tree *tree;
    lg2_common lg2;

    /** First use the config to initialize a commit signature for the user. */

    //check_lg2(git_signature_default(&sig, repo),"Unable to create a commit signature.","Perhaps 'user.name' and 'user.email' are not set");

    lg2.check_lg2(git_signature_now(&sig, user.c_str(), email.c_str()),"Could not create commit signature","");
    /* Now let's create an empty tree for this commit */


    lg2.check_lg2(git_repository_index(&index, repo),"Could not open repository index", "");

    /**
     * Outside of this example, you could call git_index_add_bypath()
     * here to put actual files into the index.  For our purposes, we'll
     * leave it empty for now.
     */

    lg2.check_lg2(git_index_write_tree(&tree_id, index),"Unable to write initial tree from index", "");
    git_index_free(index);
    lg2.check_lg2(git_tree_lookup(&tree, repo, &tree_id),"Could not look up initial tree", "");

    /**
     * Ready to create the initial commit.
     *
     * Normally creating a commit would involve looking up the current
     * HEAD commit and making that be the parent of the initial commit,
     * but here this is the first commit so there will be no parent.
     */

    lg2.check_lg2(git_commit_create_v(&commit_id, repo, "HEAD", sig, sig,NULL, "Initial project commit", tree, 0),"Could not create the initial commit", "");

    /** Clean up so we don't leak memory. */

    git_tree_free(tree);
    git_signature_free(sig);
}

enum index_mode {
    INDEX_NONE,
    INDEX_ADD,
};

struct index_options {
    int dry_run;
    int verbose;
    git_repository *repo;
    enum index_mode mode;
    int add_update;
};

static int login_tries = 1;
static bool is_cred_cached = false;


/*!
 * \fn credentials_cb
 * \brief This function is used to authenticate user
 * \details
 * Our Checks if there is some login cache or not, if not then it will check the number of login attempts
 * and after failed login attempts it will show message box that the login has failed and asks to try again (perhaps user entered invalid
 * credentials??)
 *
 * If there is cache then it wil try to login by getting the username and password and appropriate filtering and
 * conversion is applied and increment login_tries by 1, and releases the memory of userfield and passwordfield.
 * \param out
 * \param url
 * \param username_from_url
 * \param allowed_types
 * \param payload
 * \return
 */
int credentials_cb(git_cred ** out, const char *url, const char *username_from_url,
                   unsigned int allowed_types, void *payload)
{
    int error;

    /*
     * Ask the user via the UI. On error, store the information and return GIT_EUSER which will be
     * bubbled up to the code performing the fetch or push. Using GIT_EUSER allows the application
     * to know it was an error from the application instead of libgit2.
     */
    if (!is_cred_cached)
    {
    }
    QNetworkAccessManager* manager = new QNetworkAccessManager();
    QUrl url_("https://translate.udaanproject.org/udaan/email/");

    QByteArray postData;
    postData.append("username=username&password=password");

    QNetworkRequest request(url_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    // Disable SSL certificate verification
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);
    QNetworkReply* reply = manager->post(request, postData);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, [=, &loop]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonParseError errorPtr;
            QJsonDocument document = QJsonDocument::fromJson(data, &errorPtr);
            QJsonObject mainObj = document.object();
            QString git_token = mainObj.value("github_token").toString();
            QString git_username = mainObj.value("github_username").toString();
            // use git_token and git_username here
            user = git_username.toStdString();
            pass = git_token.toStdString();
            loop.quit();
        } else {
            qDebug() << "Error:" << reply->errorString();
        }
        reply->deleteLater();
    });
    loop.exec();
    return git_cred_userpass_plaintext_new(out, user.c_str(), pass.c_str());
}

/*!
 * \fn takeCredentialsFromUser
 * \brief This function takes github username and password from the user via a graphical based inteface.
 * \details It returns true if user gave credentials and false if it got cancelled
 * \return bool
 */
bool takeCredentialsFromUser()
{
    qDebug() << "Taking credentials";
    QInputDialog inp;
    bool ok = false;
    if (login_tries != 1) {
        QMessageBox msgWarning;
        msgWarning.setText("Invalid username or password. Please try again");
        msgWarning.setIcon(QMessageBox::Warning);
        msgWarning.setWindowTitle("Authentication Failed!");
        msgWarning.exec();
    }
    QDialog dialog(nullptr);
    dialog.setWindowTitle("Github Login");
    QFormLayout form(&dialog);

    QLineEdit *userfield = new QLineEdit(&dialog);
    QString userlabel = QString("Username: ");
    form.addRow(userlabel, userfield);
    QLineEdit *passfield = new QLineEdit(&dialog);
    passfield->setEchoMode(QLineEdit::Password);
    QString passlabel = QString("Password: ");
    form.addRow(passlabel, passfield);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                               Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    QObject::connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
    QObject::connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

    if (dialog.exec() == QDialog::Accepted) {
        user = userfield->text().toStdString();
        user_id=user_id.fromStdString(user);
        pass = passfield->text().toStdString();
        ok = true;
    }
    else {
        ok = false;
    }

    login_tries++;
    delete userfield;
    delete passfield;
    return ok;
}


/*!
 * \fn Project::set_corrector
 * \brief Sets the project stage as corrector
 */
void Project::set_corrector(){
    QString id=user_id;
    std::string role = id.toUtf8().constData();
    auto c = doc.child("Project").child("Metadata");
    c.child("Corrector").first_child().set_value("None");
    save_xml();
}

/*!
 * \fn Project::set_verifier
 * \brief Sets the project stage as verifier
 */
void Project::set_verifier(){
    QString id=user_id;
    std::string role = id.toUtf8().constData();
    auto c = doc.child("Project").child("Metadata");
    c.child("Verifier").first_child().set_value("None");
    save_xml();
}
int transfer_progress(const git_transfer_progress *stats, void *payload)
{
    LoadingSpinner* spinner = static_cast<LoadingSpinner*>(payload);
    QString message = QString("Importing Set... %1/%2")
        .arg(stats->received_objects)
        .arg(stats->total_objects);
    spinner->SetMessage("Importing Set...", message);
    return 0;
}

/*!
 * \fn Project::push
 * \brief This function is responsible for pushing the code to the remote github repository.
 * \details It uses libgit2 library for pushing the code.
 * 1. First store remote info from the remote repository.
 * 2. Then fetch remote objects.
 * 3. Merge fetched objects with local ones.
 * 4. Now, push the merged objects to the github repo.
 * \param branchName
 * \return bool
 */
bool Project::push(QString gDirTwoLevelUp) {
//    QDialog dialog(nullptr);
//        QFormLayout form(&dialog);
//        QProgressBar* pb;
//        pb->setParent(&dialog);
//        QLabel *label = new QLabel(&dialog);
//        QLabel *labelProgress = new QLabel(&dialog);
//        labelProgress->setText("");
//        label->setText("Pushing the changes");
//        form.addRow(label);
//        form.addWidget(pb);
//        form.addRow(labelProgress);
//        dialog.show();
    QString branchName;
    QString gDir = gDirTwoLevelUp+"/.git/config";
    lg2_add();
    git_libgit2_init();
    login_tries = 1;
    git_remote * remote = NULL;
    QFile f(gDir);
    f.open(QIODevice::ReadOnly);
    while(!f.atEnd()) {
        QString line = f.readLine();
        if(line.contains("branch")){
            QStringList l = line.split(" ");
            l[1] = l[1].remove("\"");
            branchName = l[1].remove("]").simplified();
            break;
        }
    }
    f.close();
    QByteArray array = branchName.toLocal8Bit();
    char* char_arr = array.data();
    char * refspec = (char*)"refs/heads/";
    char buffer[256];
    strncpy(buffer, refspec, sizeof(buffer));
    strncat(buffer, char_arr, sizeof(buffer));
    qDebug()<<"Buffer"<<buffer<<endl;
    char * d= (char*)buffer;
    const git_strarray refspecs = { &d,1 };

    git_fetch_options fetch_opts;
    git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;
    git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
    checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    git_annotated_commit *heads[1] = {NULL};
    git_reference *theirs_ref = NULL, *head_ref = NULL;
    git_index *index = NULL;
    git_oid id, tree_oid;
    git_signature *signature = NULL;
    git_tree *tree = NULL;
    git_commit **parents = (git_commit **)calloc(2, sizeof(git_commit *));
    git_push_options push_opts;

    // Store remote info from repo
    int error = git_remote_lookup(&remote, repo, "origin");
    if(error){
        std::cout<<0<<endl;
        //goto cleanup;
    }


    /* Fetch objects from remote repository
     * Direct pull is not available via libgit2
     */
    git_fetch_init_options(&fetch_opts, GIT_FETCH_OPTIONS_VERSION);
    fetch_opts.callbacks.credentials = credentials_cb;
    error = git_remote_fetch( remote, NULL, &fetch_opts, NULL );
    if(error){
        std::cout<<1<<endl;
        //goto cleanup;
    }
    // cache the credentials
    is_cred_cached = true;

    /* Merge fetched objects with local branch
     * It will update index and current working area
     */
    /*char* string=(char*)"refs/remotes/origin/";
    char buffer1[256];
    strncpy(buffer1, string, sizeof(buffer1));
    strncat(buffer1, char_arr, sizeof(buffer1));
    qDebug()<<"Buffer1"<<buffer1<<endl;
*/
    QString name_ref = "refs/remotes/origin/"+branchName;
    QByteArray name_ref_ar = name_ref.toLatin1();
    error = (git_reference_lookup(&theirs_ref, repo, name_ref_ar.data()) != GIT_OK);
    qDebug()<<"Error"<<error<<endl;
    if (!error)
    {
        error = (git_annotated_commit_from_ref(heads, repo, theirs_ref) != GIT_OK)
                || (git_merge(repo, (const git_annotated_commit **)heads, 1, &merge_opts, &checkout_opts) != GIT_OK);

        if(error){
            std::cout<<2<<endl;
            //goto cleanup;
        }

        /* Get the needed ref, index, sign and tree
         */
        error = (git_repository_head(&head_ref, repo))
                || (git_repository_index(&index, repo))
                || (git_signature_now(&signature, mName.c_str(), mEmail.c_str()))
                || (git_index_write_tree(&tree_oid, index))
                || (git_tree_lookup(&tree, repo, &tree_oid));

        if(error){
            std::cout<<3<<endl;
            //goto cleanup;
        }
        if(git_index_has_conflicts(index)){
            qDebug()<<"merge conflict";
            QMessageBox::information(0, "CONFLICT (content)", "Automatic merge failed; fix conflicts and then save the result. Exiting because of an unresolved conflict.");
            return false;
        }

        /* Commit the merge and cleanup repo state
         */
        error = (git_reference_peel((git_object **)&parents[0], head_ref, GIT_OBJ_COMMIT))
                || (git_commit_lookup(&parents[1], repo, git_annotated_commit_id(heads[0])))
                || (git_commit_create(&id, repo, "HEAD", signature, signature, NULL, "Merge commit - Udaan Translation Tool", tree, 2, parents))
                || (git_repository_state_cleanup(repo));

        if(error){
            std::cout << 4 << endl;
            //goto cleanup;
        }
    }
    error = git_push_init_options(&push_opts, GIT_PUSH_OPTIONS_VERSION);
    push_opts.callbacks.credentials = credentials_cb;
    error = git_remote_push(remote, &refspecs , &push_opts);
    qDebug()<<"Error"<<error<<endl;

    //std::string str="refs/remotes/origin/master";
    //char * c = (char*)"refs/heads/master";
    //const git_strarray abc = { &c,1 };
    /*cleanup:
    if(remote)
        git_remote_free(remote);
    if(heads[0])
        git_annotated_commit_free(*heads);
    if(theirs_ref)
        git_reference_free(theirs_ref);
    if(head_ref)
        git_reference_free(head_ref);
    if(index)
        git_index_free(index);
    if(signature)
        git_signature_free(signature);
    if(tree)
        git_tree_free(tree);
    if(parents)
        free(parents);
    */
    if(error<0){
        // An error occurred, print the error message
        const git_error* err = giterr_last();
        if (err) {
            std::cout << "Error: " << err->message << std::endl;
        } else {
            std::cout << "Unknown error occurred" << std::endl;
        }
        return false;
    }

    /* Finding the last commit on current repo and saves the entry in commit history table
     * Email | Commit_no
     * So that we can see users commit history based on his/her email id without github account
     */
    char fullsha[42] = {0};
    git_oid_tostr(fullsha, 41, &id);
//    push_opts.callbacks.transfer_progress = [](const git_transfer_progress* stats, void* payload)-> int {
//                     qDebug()<<"CRASHING 2";
//                     QProgressBar *progressBar = static_cast<QProgressBar*>(payload);
//                  int percentage = (100 * stats->received_objects) / stats->total_objects;
//                  progressBar->setValue(percentage);
//                  //      QMetaObject::invokeMethod(progressBar, "processProgress", Qt::QueuedConnection, Q_ARG(int, stats->received_objects), Q_ARG(int, stats->total_objects));
//                        return 0;
//    };

   //  push_opts.callbacks.payload = static_cast<void*>(pb);


    return true;//No errors
}

/*!
 * \fn progress_cb
 * \brief Returns the progress while fetching or pushing.
 * \param str
 * \param len
 * \param data
 * \return int
 */
static int progress_cb(const char *str, int len, void *data)
{
    return 0;
}

/*!
 * \fn update_cb
 * \brief It prints the information about how much data is fetched or pushed.
 * \param refname
 * \param a
 * \param b
 * \param data
 * \return int
 */
static int update_cb(const char *refname, const git_oid *a, const git_oid *b, void *data)
{
    char a_str[GIT_OID_HEXSZ + 1], b_str[GIT_OID_HEXSZ + 1];
    (void)data;

    git_oid_fmt(b_str, b);
    b_str[GIT_OID_HEXSZ] = '\0';

    if (git_oid_iszero(a))
    {
        printf("[new]     %.20s %s\n", b_str, refname);
    }
    else
    {
        git_oid_fmt(a_str, a);
        a_str[GIT_OID_HEXSZ] = '\0';
        printf("[updated] %.10s..%.10s %s\n", a_str, b_str, refname);
    }

    return 0;
}

/*!
 * \fn transfer_progress_cb
 * \brief It gives the statistics of total objects and received objects.
 * \param stats
 * \param payload
 * \return int
 */
static int transfer_progress_cb(const git_transfer_progress *stats, void *payload)
{
    (void)payload;

    if (stats->received_objects == stats->total_objects)
    {

    }
    else if (stats->total_objects > 0)
    {
    }
    return 0;
}

/*!
 * \fn Project::fetch
 * \brief This function fetches the remote objects from the remote repository. It uses the credentials to perform the fetch.
 * \details If the local repository is not up to date with the remote one, then reset the local one to the latest commit.
 * \return int
 */
int Project::fetch(QString gDirTwoLevelUp)
{
    int error = 0;
    git_remote *remote = NULL;
    //	const git_indexer_progress *stats;
    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;

    /* Figure out whether it's a named remote or a URL */
    error = git_remote_lookup(&remote, repo, "origin");
    if (error < 0) {
        error = git_remote_create_anonymous(&remote, repo, "origin");
        if (error < 0) {
            qDebug() << "error in git_remote";
            goto cleanup;
        }
    }

    /* Set up the callbacks (only update_tips for now) */
    fetch_opts.callbacks.credentials = credentials_cb;

    /**
     * Perform the fetch with the configured refspecs from the
     * config. Update the reflog for the updated references with
     * "fetch".
     */
    error = git_remote_fetch(remote, NULL, &fetch_opts, "pull");
    if (error < 0) {
        qDebug() << "error in fetch";
        goto cleanup;
    }
    is_cred_cached = true;

    //	stats = git_remote_stats(remote);
    //	char buffer[200];
    //	int ck;

    //	if (stats->local_objects > 0) {
    //		ck = snprintf(buffer, 200, "Received %u/%u objects in %zu bytes (used %u local objects)",
    //				 stats->indexed_objects, stats->total_objects, stats->received_bytes, stats->local_objects);
    //	} else {
    //		ck = snprintf(buffer, 200, "Received %u/%u objects in %zu bytes\n",
    //			   stats->indexed_objects, stats->total_objects, stats->received_bytes);
    //	}
    //	if (ck >= 0 && ck < 200) {
    //		qDebug() << QString::fromLocal8Bit(buffer);
    //	}

    /**
     * 1. Check if the repository is already up to date (Don't perform git reset)
     * 2. If it is not up to date then reset the repo to the latest commit (This will delete the modifications done by user)
     */

    git_revwalk *walker;
    error = git_revwalk_new(&walker, repo);
    if (error < 0) {
        goto cleanup;
    }
    error = git_revwalk_push_range(walker, "HEAD..refs/remotes/origin/HEAD");
    if (error < 0) {
        goto cleanup;
    }

    git_oid oid;
    int count;
    count = 0;
    while(!git_revwalk_next(&oid, walker)) {
        count++;
    }
    git_revwalk_free(walker);
    qDebug() << "Final count = " << count;
    if (count > 0) {
        //find local branch name
        QString branchName;
        QString gDir = gDirTwoLevelUp+"/.git/config";
        QFile f(gDir);
        f.open(QIODevice::ReadOnly);
        while(!f.atEnd()) {
            QString line = f.readLine();
            if(line.contains("branch")){
                QStringList l = line.split(" ");
                l[1] = l[1].remove("\"");
                branchName = l[1].remove("]").simplified();
                break;
            }
        }
        f.close();
        // perform git merge
        git_reference *local_branch_ref = NULL;
        error = git_branch_lookup(&local_branch_ref, repo, branchName.toUtf8().constData(), GIT_BRANCH_LOCAL);
        if (error < 0) {
            qDebug() << "Cannot find local branch";
            goto cleanup;
        }

        git_annotated_commit *remote_commit = NULL;
        error = git_annotated_commit_lookup(&remote_commit, repo, &oid);
        if (error < 0) {
            qDebug() << "Cannot find remote commit";
            goto cleanup;
        }

        git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;
        error = git_merge(repo, (const git_annotated_commit **) &remote_commit, 1, &merge_opts, NULL);
        if (error < 0) {
            qDebug() << "Cannot merge changes from remote branch";
            goto cleanup;
        }

        git_reference_free(local_branch_ref);
        git_annotated_commit_free(remote_commit);
    }

cleanup:
    git_remote_free(remote);
    return error;
}

/*!
 * \fn Project::commit
 * \brief This function commits the changes done to the local repository.
 * \param message
 * \return bool
 */
bool Project::commit(std::string message)
{
    lg2_common lg2;
    lg2_add();
    git_signature *sig;
    git_index *index;
    git_oid tree_id, commit_id;
    git_tree *tree;

    git_object *parent = NULL;
    git_reference *ref = NULL;
    /** First use the config to initialize a commit signature for the user. */
    //check_lg2(git_signature_default(&sig, repo),"Unable to create a commit signature.","Perhaps 'user.name' and 'user.email' are not set");
    int klass = lg2.check_lg2(git_signature_now(&sig, mName.c_str(), mEmail.c_str()),"Could not create signature","");
    if (klass > 0) {
        //        if(sig)
        //            git_signature_free(sig);
        return 0;
    }

    klass = lg2.check_lg2(git_revparse_ext(&parent, &ref, repo, "HEAD"),"Head not found","");
    if (klass > 0)
    {
        return 0;
    }
    /* Now let's create an empty tree for this commit */

    klass = lg2.check_lg2(git_repository_index(&index, repo), "Could not open repository index", "");
    if (klass > 0) {
        return 0;
    }

    /**
     * Outside of this example, you could call git_index_add_bypath()
     * here to put actual files into the index.  For our purposes, we'll
     * leave it empty for now.
     */

    klass = lg2.check_lg2(git_index_write_tree(&tree_id, index), "Could not write tree", "");;
    if (klass > 0)
    {
        return 0;
    }

    klass = lg2.check_lg2(git_index_write(index), "Could not write index", "");;
    if (klass > 0)
    {
        return 0;
    }

    klass = lg2.check_lg2(git_tree_lookup(&tree, repo, &tree_id), "Error looking up tree", "");
    if (klass > 0)
    {
        return 0;
    }

    klass = lg2.check_lg2(git_signature_now(&sig, mName.c_str(), mEmail.c_str()), "Error creating signature", "");
    if (klass > 0) {
        return 0;
    }

    /**
     * Ready to create the initial commit.
     *
     * Normally creating a commit would involve looking up the current
     * HEAD commit and making that be the parent of the initial commit,
     * but here this is the first commit so there will be no parent.
     */

    klass = lg2.check_lg2(git_commit_create_v(&commit_id, repo, "HEAD", sig, sig, NULL, message.c_str(), tree, parent ? 1 : 0, parent), "Could not create commit", "");
    if (klass > 0) {
        git_tree_free(tree);
        git_signature_free(sig);
        git_index_free(index);
        return 0;
    }
    char fullsha[42] = {0};
    git_oid_tostr(fullsha, 41, &commit_id);
    QString sha = QString::fromStdString(fullsha);
    //    qDebug()<<"Last commit full hash :"<<sha;
    QSettings settings("IIT-B", "OpenOCRCorrect");
    settings.beginGroup("login");
    QString email = settings.value("email").toString();
    //qDebug()<<"email"<<email;
    settings.endGroup();
    QNetworkAccessManager* manager = new QNetworkAccessManager();
    QUrl url_("https://translate.udaanproject.org/udaan/commits/");

    QByteArray postData;
    postData.append("commit_no="+sha+"&email="+email);

    QNetworkRequest request(url_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    // Disable SSL certificate verification
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);
    QNetworkReply* reply = manager->post(request, postData);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, [=, &loop]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            loop.quit();
        } else {
            qDebug() << "Error:" << reply->errorString();
        }
        reply->deleteLater();
    });
    loop.exec();
    /** Clean up so we don't leak memory. */

    git_tree_free(tree);
    git_signature_free(sig);
    git_index_free(index);
    return 1;
}

/*!
 * \fn Project::add_config
 * \brief It takes the system level config from the git if git is installed and stores them into variables.
 * \return bool
 */
bool Project::add_config() {
    git_config * cfg = NULL;
    git_config* sys_cfg = NULL;
    git_config_entry* entry = NULL;
    std::string user = "";
    std::string email = "";
    lg2_common lg2;
    int error = git_repository_config(&cfg, repo);
    lg2.check_lg2(error, "Couldn't open config file", "");

    error = git_config_open_level(&sys_cfg, cfg, GIT_CONFIG_LEVEL_SYSTEM);
    lg2.check_lg2(error, "Couldn't open system level config", "");

    error = git_config_get_entry(&entry, sys_cfg, "user.name");
    lg2.check_lg2(error, "Couldn't get user.name", "");
    std::string str = "";
    if (entry) {
        str = entry->value;
    }
    else {
        //add username
        bool ok = false;
        QString quser = QInputDialog::getText(nullptr, QWidget::tr("Enter name:"),
                                              QWidget::tr("Username:"), QLineEdit::Normal,
                                              " ", &ok);

        str = quser.toStdString();
        error = git_config_set_string(sys_cfg, "user.name", (char*)str.c_str());
        lg2.check_lg2(error, "Could not set user.name", "");
        if (!ok || error < 0)
            return false;

    }
    mName = str;

    error = git_config_get_entry(&entry, sys_cfg, "user.email");
    lg2.check_lg2(error, "Couldn't get user.email", "");
    if (entry) {
        str = entry->value;
    }
    else
    {
        //add username
        bool ok = false;
        QString quser = QInputDialog::getText(nullptr, QWidget::tr("Enter email:"),
                                              QWidget::tr("Email:"), QLineEdit::Normal,
                                              " ", &ok);
        std::string str = quser.toStdString();
        error = git_config_set_string(sys_cfg, "user.email", (char*)str.c_str());
        lg2.check_lg2(error, "Could not set user.email", "");
        if (!ok || error < 0)
            return false;

    }
    mEmail = str;
    git_config_free(cfg);
    git_config_free(sys_cfg);
    return true;
}

/*!
 * \fn Project::add_git_config
 * \brief It adds the git config to the variables.
 * \return bool
 */
bool Project::add_git_config()
{
    bool ret = true;
    mName = "anujadumada8";
    mEmail = "anujadumada08@gmail.com";
    user = "anujadumada8";
    email = "anujadumada08@gmail.com";
    //    QSettings appSettings("IIT-B", "OpenOCRCorrect");
    //    appSettings.beginGroup("GitConfig");
    //    QString username = appSettings.value("user", "").toString();
    //    QString email = appSettings.value("email", "").toString();
    //    if (username == "" || email == "") {
    //        // Take input from user and set the git config
    ////        bool ok = false;
    //        QString quser = "anujadumada8"; //QInputDialog::getText(nullptr, QWidget::tr("Enter username:"), QWidget::tr("Username:"), QLineEdit::Normal, "", &ok);
    ////        if (!ok) {
    ////            ret = ok;
    ////            goto exit;
    ////        }
    //        QString qemail = "anujadumada08@gmail.com";//QInputDialog::getText(nullptr, QWidget::tr("Enter email:"), QWidget::tr("Email:"), QLineEdit::Normal, "", &ok);
    ////        if (!ok) {
    ////            ret = ok;
    ////            goto exit;
    ////        }

    ////        if (quser.trimmed() == "" || qemail.trimmed() == "" || !qemail.contains("@")) {
    ////            ret = false;
    ////            goto exit;
    ////        }

    //        appSettings.setValue("user", quser);
    //        appSettings.setValue("email", qemail);

    //        mName = quser.toStdString();
    //        mEmail = qemail.toStdString();
    //    } else {
    //        mName = username.toStdString();
    //        mEmail = email.toStdString();
    //    }

    //exit:
    //    appSettings.endGroup();

    return ret;
}

/*!
 * \fn match_cb
 * \brief It gives the information of how much remote and local objects match.
 * \param path
 * \param spec
 * \param payload
 * \return int
 */
int match_cb(const char *path, const char *spec, void *payload) {

    //match_data *d = (match_data*)payload;
    /*
     * return 0 to add/remove this path,
     * a positive number to skip this path,
     * or a negative number to abort the operation.
     */
    std::string spath = path;
    return 0;
}

/*!
 * \fn Project::lg2_add
 * \brief It adds the "workingFolder" to current git index so that they can be tracked and later pushed.
 * \param workingFolder
 */
void Project::lg2_add(QString workingFolder)
{
    lg2_common lg2;
    const char * paths[] = { "/Dicts" ,"/Comments","/Images", workingFolder.toUtf8().constData()};
    git_strarray arr = { (char**)paths,1 };
    git_index *idx = NULL;
    int error = git_repository_index(&idx, repo);
    lg2.check_lg2(error, "Error Could not open index", "");
    error = git_index_add_all(idx, &arr, GIT_INDEX_ADD_DEFAULT, match_cb, nullptr);
    lg2.check_lg2(error, "Error could not add", "");
    error = git_index_update_all(idx, &arr, match_cb, nullptr);
    lg2.check_lg2(error, "Error could not update", "");
    git_index_write(idx);
    git_index_free(idx);
}

/*!
 * \fn Project::lg2_add
 * \brief It adds everything in the current directory to the current git index.
 */
void Project::lg2_add() {
    const char * paths[] = { "/*"};
    git_strarray arr = { (char**)paths,1 };
    git_index *idx = NULL;\
    lg2_common lg2;
    int error = git_repository_index(&idx, repo);
    lg2.check_lg2(error, "Error Could not open index", "");
    error = git_index_add_all(idx, &arr, GIT_INDEX_ADD_DEFAULT, match_cb, nullptr);
    lg2.check_lg2(error, "Error could not add", "");
    error = git_index_update_all(idx, &arr, match_cb, nullptr);
    lg2.check_lg2(error, "Error could not update", "");
    git_index_write(idx);
    git_index_free(idx);
}

/*!
 * \fn Project::add_and_commit
 * \brief Adds and commits the file
 */
void Project::add_and_commit() {

}

/*!
 * \fn Project::getFilter
 * \brief Returns the pointer to the Filter whose name matches to the string passed otherwise returns nullptr.
 * \param str
 * \return Filter*
 */
Filter * Project::getFilter(QString str)
{
    for (auto * p : mFilters) {
        if (p->name() == str) {
            return p;
        }
    }
    return nullptr;
}

/*!
 * \fn Project::get_configuration
 * \brief Returns the configuration from project.xml
 * \return QString
 */
QString Project::get_configuration()
{
    auto c = doc.child("Project").child("Configuration");
    QString PMatch = c.child("Prefixmatch").child_value();
    return PMatch;
}

/*!
 * \fn Project::get_stage
 * \brief Returns the stage from the project.xml
 * \return QString
 */
QString Project::get_stage()
{
    auto c = doc.child("Project").child("Metadata");
    QString stage = c.child("Stage").child_value();
    return stage;
}

/*!
 * \fn Project::get_version
 * \brief Returns the version of the set.
 * \return QString
 */
QString Project::get_version()
{
    auto c = doc.child("Project").child("Metadata");
    QString version = c.child("Version").child_value();
    return version;
}

/*!
 * \fn Project::get_pmEmail
 * \brief Returns the project manager email
 * \return QString
 */
QString Project::get_pmEmail()
{
    auto c = doc.child("Project").child("Metadata");
    QString stage = c.child("ProjectManagerEmail").child_value();
    return stage;
}

/*!
 * \fn Project::get_bookId
 * \brief Returns the book id
 * \return QString
 */
QString Project::get_bookId() {
    auto c = doc.child("Project").child("Metadata");
    QString version = c.child("BookId").child_value();
    return version;
}

/*!
 * \fn Project::get_setId
 * \brief Returns the set id
 * \return QString
 */
QString Project::get_setId()
{
    auto c = doc.child("Project").child("Metadata");
    QString stage = c.child("SetId").child_value();
    return stage;
}

/*!
 * \fn Project::get_repo
 * \brief Returns the repository link of the set
 * \return QString
 */
QString Project::get_repo()
{
    auto c = doc.child("Project").child("Metadata");
    QString stage = c.child("RepositoryLink").child_value();
    return stage;
}

/*!
 * \fn Project::open_git_repo
 * \brief Opens the git repo under the hood in a git_repository variable
 */
void Project::open_git_repo()
{
    std::string dir = mProjectDir.path().toStdString();
    QString gitpath = mProjectDir.path() + "/.git";
    QDir gitdir(gitpath);
    lg2_common lg2;
    git_signature * out;

    if (gitdir.exists())
    {
        lg2.check_lg2(git_repository_open(&repo, dir.c_str()), "Failed to Open", "");
        //        add_config();
        add_git_config();
    }
    else
    {
        lg2.check_lg2(git_repository_init(&repo, dir.c_str(),0), "Failed to Open", "");
        //        add_config();
        add_git_config();
        lg2_add();
        create_initial_commit(repo);

    }
}

/*!
 * \fn Project::GetGraphemesCount
 * \brief Returns the number of graphemes in a string
 * \param string
 * \return int
 */
int Project::GetGraphemesCount(QString string)
{
    int count = 0;
    QTextBoundaryFinder finder = QTextBoundaryFinder(QTextBoundaryFinder::BoundaryType::Grapheme, string);
    while (finder.toNextBoundary() != -1) {
        count++;
    }
    int spaces = string.count(' ');
    return count - spaces;
}

/*!
 * \fn Project::LevenshteinWithGraphemes
 * \brief Returns the Levenshtein distance between the diffs
 * \param diffs
 * \return int
 */
int Project::LevenshteinWithGraphemes(QList<Diff> diffs)
{
    int levenshtein = 0;
    QString diffChars = "";
    QString insertions = "";
    QString deletions = "";
    foreach(Diff aDiff, diffs) {
        switch (aDiff.operation) {
        case INSERT:
            insertions += aDiff.text;
            break;
        case DELETE:
            deletions += aDiff.text;
            break;
        case EQUAL:
            // A deletion and an insertion is one substitution.
            if(GetGraphemesCount(insertions)> GetGraphemesCount(deletions))
                levenshtein += insertions.length();
            else
                levenshtein += deletions.length();
            insertions = "";
            deletions = "";
            break;
        default:
            break;
        }
    }
    if(GetGraphemesCount(insertions)> GetGraphemesCount(deletions))
        levenshtein += insertions.length();
    else
        levenshtein += deletions.length();
    return levenshtein;
}

/*!
 * \fn Project::GetPageNumber
 * \brief Returns the page number from a given filename
 * \param localFilename
 * \param no
 * \param loc
 * \param ext
 * \return int
 */
int Project::GetPageNumber(std::string localFilename, std::string *no, size_t *loc, QString *ext)
{

    std::string nos = "0123456789";
    *no = "";
    *loc = localFilename.find(".txt");
    *ext = "txt";
    if(*loc == std::string::npos) {
        *loc = localFilename.find(".html");
        *ext = "html";
    }
    if(*loc == std::string::npos)
        return 0;
    std::string s = localFilename.substr((*loc)-1,1);
    while(nos.find(s) != std::string::npos) {
        *no = s + *no; (*loc)--; s = localFilename.substr((*loc)-1,1);
    }
    return 1;
}

/*!
 * \fn Project::clone
 * \brief It clones the git repository from the "url_" and stores the repository in the path "path" passes.
 * \details Returns the success code
 * \param url_
 * \param path
 * \return int
 */
int Project::clone(QString url_, QString path, LoadingSpinner* spinner)
{
    QByteArray array = url_.toLocal8Bit();
    const char *url = array.data();
    /*git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
    clone_opts.fetch_opts.callbacks.credentials = credentials_cb;
    clone_opts.fetch_opts.callbacks.transfer_progress = &Project::progress_callback;
    clone_opts.fetch_opts.callbacks.payload = spinner;*/
    git_repository *repo = NULL;
    git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
    clone_opts.fetch_opts.callbacks.credentials = credentials_cb;
    clone_opts.fetch_opts.callbacks.transfer_progress = transfer_progress;
    clone_opts.fetch_opts.callbacks.payload = spinner;

    QStringList list = url_.split("/");
    QString repoName = list[list.size() - 1].remove(".git");
    path = path + "/" + repoName;

    int error = git_clone(&repo, url, path.toLocal8Bit().data(), &clone_opts);

    if(error < 0) {
        qDebug() << "Error importing the project";
    }
    else {
        qDebug() << " Imported successfully";
    }
    return error;
}

bool Project::fetch_n_merge(QString gDirTwoLevelUp, QString mRole) {
    int error = 0;
    git_remote *remote = NULL;
    git_reference *local_branch_ref = NULL;
    git_annotated_commit *remote_commit = NULL;
    git_revwalk *walker = NULL;

    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    fetch_opts.callbacks.credentials = credentials_cb;

    git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;
    QString branchName;
    QString gDir = gDirTwoLevelUp+"/.git/config";
    QFile f(gDir);QString folderPath;
    if(mRole =="Verifier")
        folderPath = gDirTwoLevelUp + "/CorrectorOutput/"; // specify the folder path
    else if(mRole == "Corrector")
        folderPath = gDirTwoLevelUp + "/VerifierOutput/"; // specify the folder path
    QDir dir(folderPath);
    QFileInfoList fileList = dir.entryInfoList(QDir::Files);

    // Check if 'origin' remote exists, if not create an anonymous remote
    error = git_remote_lookup(&remote, repo, "origin");
    if (error < 0) {
        error = git_remote_create_anonymous(&remote, repo, "origin");
        if (error < 0) {
            qDebug() << "Error in git_remote";
            goto cleanup;
        }
    }

    // Perform the fetch with the configured refspecs from the config
    error = git_remote_fetch(remote, NULL, &fetch_opts, "pull");
    if (error < 0) {
        qDebug() << "Error in fetch";
        goto cleanup;
    }

    // Check if the repository is already up to date
    git_oid local_oid, remote_oid;
    error = git_reference_name_to_id(&local_oid, repo, "HEAD");
    if (error < 0) {
        qDebug() << "Error getting local OID";
        goto cleanup;
    }
    error = git_reference_name_to_id(&remote_oid, repo, "refs/remotes/origin/HEAD");
    if (error < 0) {
        qDebug() << "Error getting remote OID";
        goto cleanup;
    }

    if (git_oid_equal(&local_oid, &remote_oid)) {
        qDebug() << "Repository is already up to date";
        git_reference_free(local_branch_ref);
        git_annotated_commit_free(remote_commit);
        git_remote_free(remote);
        return true;
    }

    // update the local branch to the latest commit

    f.open(QIODevice::ReadOnly);
    while(!f.atEnd()) {
        QString line = f.readLine();
        if(line.contains("branch")){
            QStringList l = line.split(" ");
            l[1] = l[1].remove("\"");
            branchName = l[1].remove("]").simplified();
            break;
        }
    }
    f.close();

    error = git_branch_lookup(&local_branch_ref, repo, branchName.toUtf8().constData(), GIT_BRANCH_LOCAL);
    if (error < 0) {
        qDebug() << "Cannot find local branch";
        goto cleanup;
    }

    error = git_annotated_commit_lookup(&remote_commit, repo, &remote_oid);
    if (error < 0) {
        qDebug() << "Cannot find remote commit";
        goto cleanup;
    }

    if(error<0){
        // An error occurred, print the error message
        const git_error* err = giterr_last();
        if (err) {
            std::cout << "Error: " << err->message << std::endl;
        } else {
            std::cout << "Unknown error occurred" << std::endl;
        }
    }
    // change permissions of files

    for (const QFileInfo& fileInfo : fileList) {
        QString filePath = fileInfo.filePath();
        // Set the file's permissions to both read and write mode
        QFile::setPermissions(filePath, QFile::WriteOwner | QFile::WriteGroup | QFile::WriteOther |
                              QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    error = git_merge(repo, (const git_annotated_commit **)&remote_commit, 1, &merge_opts, NULL);
    for (const QFileInfo& fileInfo : fileList) {
        QString filePath = fileInfo.filePath();
        // Set the file's permissions to read only
        QFile::setPermissions(filePath, QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    if (error < 0) {
        qDebug() << "Cannot merge changes from remote branch";
        // An error occurred, print the error message
        const git_error* err = giterr_last();
        if (err) {
            std::cout << "Error: " << err->message << std::endl;
        } else {
            std::cout << "Unknown error occurred" << std::endl;
        }
        goto cleanup;
    }

    qDebug() << "Changes merged successfully";
    git_reference_free(local_branch_ref);
    git_annotated_commit_free(remote_commit);
    git_remote_free(remote);
    return true;

cleanup:
    git_reference_free(local_branch_ref);
    git_annotated_commit_free(remote_commit);
    git_remote_free(remote);
    return false;
}


#ifdef _WIN32
/*!
 * \fn Project::findNumberOfFilesInDirectory
 * \brief Returns the number of files in a directory
 * \details It uses "dir" command for counting files
 * \param path
 * \return int
 */
int Project::findNumberOfFilesInDirectory(std::string path)
{
    FILE* fp;
    int file_count;
    replace(path.begin(), path.end(), '/', '\\');

    //   std::string command = R"(dir /b /a-s-d ")" + path + R"(" | find /c /v "")"; //  non-recursive count -> files in sub-directories will not be counted
    std::string command = R"(dir /b /s /a-s-d ")" + path + R"(" | find /c /v "")"; // recursive count
    fp = _popen(command.c_str(), "r");
    if (!fp) {
        std::cout << "Failed to count files at " << path << std::endl;
        return -1;
    }

    fscanf_s(fp, "%d", &file_count);

    _pclose(fp);
    return file_count;
}
#endif

#ifdef __unix__
/*!
 * \fn Project::findNumberOfFilesInDirectory
 * \brief Returns the number of files in a directory
 * \details It uses "find" command for counting files
 * \param path
 * \return int
 */
int Project::findNumberOfFilesInDirectory(std::string path)
{
    FILE* fp;
    int file_count;

    //    std::string command = R"(find ")" + path + R"(" -maxdepth 1 -not -path '*/\.*' -type f | wc -l)"; //  non-recursive count -> files in sub-directories will not be counted
    std::string command = R"(find ")" + path + R"(" -not -path '*/\.*' -type f | wc -l)"; // recursive count

    fp = popen(command.c_str(), "r");
    if (!fp) {
        std::cout << "Failed to count files at " << path << std::endl;
        return -1;
    }
    fscanf(fp, "%d", &file_count);
    pclose(fp);
    return file_count;

}
#endif

/*!
 * \fn Project::describe_commit
 * \brief Returns the names of changed files in the particular commit number
 * \details It is equivalent to "git show --name-only {commit number}" command on git bash
 * \param mRole, commit_number
 * \return QString 
 */
QString Project::describe_commit(QString mRole,QString commit_number)
{
    QByteArray array = commit_number.toLocal8Bit();
    const char * commit_no = array.data();
    QString changedFiles = "No Changed Files";
    git_commit *commit = NULL;

    int error = git_revparse_single((git_object **)&commit, repo, commit_no);
    if (error < 0) {
        qDebug() << "Error parsing commit: " << git_error_last()->message;
        return changedFiles;
    }

    size_t parent_count = git_commit_parentcount(commit);
    if (parent_count > 0) {
        git_commit *parent = NULL;
        error = git_commit_parent(&parent, commit, 0);
        if (error >= 0) {
            git_tree *tree, *parent_tree;
            error = git_commit_tree(&tree, commit);
            if (error >= 0) {
                error = git_commit_tree(&parent_tree, parent);
                if (error >= 0) {
                    git_diff *diff;
                    error = git_diff_tree_to_tree(&diff, repo, parent_tree, tree, NULL);
                    if (error >= 0) {
                        size_t num_deltas = git_diff_num_deltas(diff);
                        for (size_t i = 0; i < num_deltas; ++i) {
                            const git_diff_delta *delta = git_diff_get_delta(diff, i);
                            QString str = delta->new_file.path;
                            if(mRole == "Corrector" && !str.contains("CorrectorOutput")){
                                continue;
                            }
                            else{
                                str.remove("CorrectorOutput");
                            }
                            if(mRole == "Verifier" && !str.contains("VerifierOutput")){
                                continue;
                            }
                            else{
                                str.remove("VerifierOutput");
                            }

                            if(changedFiles == "No Changed Files")
                                changedFiles = str;
                            else
                                changedFiles += str;
                            changedFiles.append("\n");
                        }
                        git_diff_free(diff);
                    }
                }
                git_tree_free(parent_tree);
            }
            git_tree_free(tree);
            git_commit_free(parent);
        }
    }

    git_commit_free(commit);

    return changedFiles;
}
